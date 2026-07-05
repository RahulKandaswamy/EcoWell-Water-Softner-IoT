#include <Arduino.h>
#include "ModbusServerRTU.h"

// ============= PIN CONFIGURATION =================
#define RS485_RX_PIN  18
#define RS485_TX_PIN  17
#define RS485_DE_PIN  -1   // Set to actual GPIO if your RS485 board has DE/RE pin

#define SLAVE_ID      1
#define BAUD_RATE     19200

// ============= SIMULATED PROCESS DATA ============
static float    sim_water_flow     = 12.5f;    // L/min
static float    sim_water_pressure = 45.0f;    // psi
static float    sim_salt_level     = 75.0f;    // %
static uint16_t sim_tds            = 250;      // ppm
static bool     sim_power_status   = true;
static bool     sim_regen_status   = false;

// ============= REGISTER STORAGE ==================
// 8 Holding Registers (protocol address 0–7 → Modbus 40001–40008)
static uint16_t holdingRegs[8] = {0};

// 2 Discrete Inputs (protocol address 0–1 → Modbus 10001–10002)
static bool discreteInputs[2] = {false, false};

// ============= eModbus Server ====================
ModbusServerRTU MBserver(2000, RS485_DE_PIN);

// ============= FLOAT ENCODING (CDAB Word Order) ==
// Gateway expects CDAB: Low word first, High word second
static void encodeFloat_CDAB(uint16_t regIndex, float value){
  uint32_t raw;
  memcpy(&raw, &value, sizeof(float));
  holdingRegs[regIndex]     = raw & 0xFFFF;           // Low word  (CD)
  holdingRegs[regIndex + 1] = (raw >> 16) & 0xFFFF;   // High word (AB)
}

static void refreshRegisters(){
  encodeFloat_CDAB(0, sim_water_flow);        // 40001–40002
  encodeFloat_CDAB(2, sim_water_pressure);    // 40003–40004
  encodeFloat_CDAB(4, sim_salt_level);        // 40005–40006
  holdingRegs[6] = sim_tds;                    // 40007
  holdingRegs[7] = 0;                          // 40008 (spare)

  discreteInputs[0] = sim_power_status;       // 10001
  discreteInputs[1] = sim_regen_status;       // 10002
}

// ============= FC03: Read Holding Registers ======
ModbusMessage FC03(ModbusMessage request){
  ModbusMessage response;
  uint16_t addr  = 0;
  uint16_t words = 0;
  request.get(2, addr);
  request.get(4, words);

  // Bounds check
  if(addr + words > 8){
    response.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
    return response;
  }

  refreshRegisters();

  // Response: [serverID][FC][byteCount][reg0_hi][reg0_lo]...
  response.add(request.getServerID(), request.getFunctionCode(), (uint8_t)(words * 2));
  for(uint16_t i = 0; i < words; i++){
    response.add(holdingRegs[addr + i]);
  }

  Serial.printf("[FC03] Read %u registers from addr %u\n", words, addr);
  return response;
}

// ============= FC02: Read Discrete Inputs ========
ModbusMessage FC02(ModbusMessage request){
  ModbusMessage response;
  uint16_t addr = 0;
  uint16_t bits = 0;
  request.get(2, addr);
  request.get(4, bits);

  // Bounds check
  if(addr + bits > 2){
    response.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
    return response;
  }

  refreshRegisters();

  // Pack discrete inputs into a single byte
  uint8_t byteCount = (bits + 7) / 8;
  uint8_t bitData   = 0;
  for(uint16_t i = 0; i < bits; i++){
    if(discreteInputs[addr + i]){
      bitData |= (1 << i);
    }
  }

  // Response: [serverID][FC][byteCount][bitData]
  response.add(request.getServerID(), request.getFunctionCode(), byteCount);
  response.add(bitData);

  Serial.printf("[FC02] Read %u discrete inputs from addr %u → 0x%02X\n", bits, addr, bitData);
  return response;
}

// ============= FC05: Write Single Coil ===========
ModbusMessage FC05(ModbusMessage request){
  ModbusMessage response;
  uint16_t addr  = 0;
  uint16_t value = 0;
  request.get(2, addr);
  request.get(4, value);

  // We only support coil at address 0 (Modbus coil 1 = cmd_regen)
  if(addr > 1){
    response.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
    return response;
  }

  bool cmd = (value == 0xFF00);

  if(cmd){
    Serial.println("========================================");
    Serial.println("  >>> REGEN START command received! <<<");
    Serial.println("========================================");
    sim_regen_status = true;
  } else {
    Serial.println("========================================");
    Serial.println("  >>> REGEN STOP command received!  <<<");
    Serial.println("========================================");
    sim_regen_status = false;
  }

  refreshRegisters();

  // FC05 response is an echo of the request
  response.add(request.getServerID(), request.getFunctionCode());
  response.add(addr);
  response.add(value);

  return response;
}

// ============= SERIAL COMMAND INTERFACE ==========
void processSerialCommand(){
  if(!Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();

  if(cmd.startsWith("flow ")){
    sim_water_flow = cmd.substring(5).toFloat();
    Serial.printf("[SIM] Water Flow = %.2f L/min\n", sim_water_flow);
  }
  else if(cmd.startsWith("pressure ")){
    sim_water_pressure = cmd.substring(9).toFloat();
    Serial.printf("[SIM] Pressure = %.2f psi\n", sim_water_pressure);
  }
  else if(cmd.startsWith("salt ")){
    sim_salt_level = cmd.substring(5).toFloat();
    Serial.printf("[SIM] Salt Level = %.2f %%\n", sim_salt_level);
  }
  else if(cmd.startsWith("tds ")){
    sim_tds = cmd.substring(4).toInt();
    Serial.printf("[SIM] TDS = %u ppm\n", sim_tds);
  }
  else if(cmd == "power on"){
    sim_power_status = true;
    Serial.println("[SIM] Power = ON");
  }
  else if(cmd == "power off"){
    sim_power_status = false;
    Serial.println("[SIM] Power = OFF");
  }
  else if(cmd == "regen on"){
    sim_regen_status = true;
    Serial.println("[SIM] Regen Status = RUNNING");
  }
  else if(cmd == "regen off"){
    sim_regen_status = false;
    Serial.println("[SIM] Regen Status = STOPPED");
  }
  else if(cmd == "fault"){
    sim_water_pressure = 10.0f;
    Serial.println("[SIM] FAULT INJECTED: Pressure = 10.0 psi!");
  }
  else if(cmd == "recover"){
    sim_water_pressure = 45.0f;
    sim_salt_level = 75.0f;
    Serial.println("[SIM] RECOVERED: Pressure = 45.0, Salt = 75.0");
  }
  else if(cmd == "status"){
    Serial.println("======= SLAVE STATUS =======");
    Serial.printf("  Flow:     %.2f L/min\n", sim_water_flow);
    Serial.printf("  Pressure: %.2f psi\n",   sim_water_pressure);
    Serial.printf("  Salt:     %.2f %%\n",     sim_salt_level);
    Serial.printf("  TDS:      %u ppm\n",      sim_tds);
    Serial.printf("  Power:    %s\n",           sim_power_status ? "ON" : "OFF");
    Serial.printf("  Regen:    %s\n",           sim_regen_status ? "RUNNING" : "STOPPED");
    Serial.println("============================");
  }
  else if(cmd == "help"){
    Serial.println("===== COMMANDS =====");
    Serial.println("  flow <val>     - Set water flow (L/min)");
    Serial.println("  pressure <val> - Set pressure (psi)");
    Serial.println("  salt <val>     - Set salt level (%)");
    Serial.println("  tds <val>      - Set TDS (ppm)");
    Serial.println("  power on/off   - Toggle power status");
    Serial.println("  regen on/off   - Toggle regen status");
    Serial.println("  fault          - Inject low-pressure fault");
    Serial.println("  recover        - Reset to normal values");
    Serial.println("  status         - Print all values");
    Serial.println("  help           - Show this menu");
    Serial.println("====================");
  }
  else{
    Serial.println("[SIM] Unknown command. Type 'help'");
  }

  refreshRegisters();
}

// ============= SETUP & LOOP =====================
void setup(){
  Serial.begin(115200);
  delay(1000);

  Serial.println("=====================================");
  Serial.println("  EcoWell Modbus Slave Simulator v1.0");
  Serial.printf("  Slave ID: %d | Baud: %d 8N2\n", SLAVE_ID, BAUD_RATE);
  Serial.println("  Type 'help' for commands");
  Serial.println("=====================================");

  // Start RS485 Serial
  Serial2.begin(BAUD_RATE, SERIAL_8N2, RS485_RX_PIN, RS485_TX_PIN);

  // Register Modbus workers BEFORE begin()
  MBserver.registerWorker(SLAVE_ID, READ_HOLD_REGISTER, &FC03);
  MBserver.registerWorker(SLAVE_ID, READ_DISCR_INPUT,   &FC02);
  MBserver.registerWorker(SLAVE_ID, WRITE_COIL,         &FC05);

  // Load initial register values
  refreshRegisters();

  // Start the Modbus server (creates internal FreeRTOS task)
  MBserver.begin(Serial2);

  Serial.println("[SLAVE] Modbus Server started. Waiting for requests...");
}

void loop(){
  processSerialCommand();
  delay(10);
}