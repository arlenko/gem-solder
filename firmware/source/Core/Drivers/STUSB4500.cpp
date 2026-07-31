#include "STUSB4500.hpp"
#include "FreeRTOS.h"
#include "I2CBB2.hpp"
#include "configuration.h"
#include "task.h"

#ifdef POW_PD_STUSB4500

static STUSB_PD_SNK_PDO_TypeDef sinkPDO[3]; // Device PDOs
static STUSB_PD_SRC_PDO_TypeDef srcPDO[7];  // Source PDOs
static uint8_t                  srcPDOCount;

enum class NegotiationState : uint8_t { NotStarted, GettingCapabilities, HaveCapabilities, Negotiating, Done };
static NegotiationState negotiationState = NegotiationState::NotStarted;

extern int32_t powerSupplyWattageLimit;

// I2C helpers
bool stusb_read(uint8_t reg, uint8_t *data, uint16_t len) { return I2CBB2::Mem_Read(STUSB4500_ADDR, reg, data, len); }
bool stusb_write(uint8_t reg, const uint8_t *data, uint16_t len) {
  return I2CBB2::Mem_Write(STUSB4500_ADDR, reg, data, len);
}

// Check if the device is present on I2C bus by reading it's id and compare to the expected value
bool STUSB4500::probe() {
  if (!I2CBB2::probe(STUSB4500_ADDR))
    return false;
  uint8_t device_id;
  if (!stusb_read(STUSB_REG_DEVICE_ID, &device_id, 1))
    return false;
  return device_id == STUSB4500_ID || device_id == STUSB4500_ID_B;
}

// Read PDOs and clear alerts
void STUSB4500::init() {
  // Wait a few ms after power up
  while (xTaskGetTickCount() < pdMS_TO_TICKS(50)) {
    vTaskDelay(10);
  }

  if (!probe())
    return;

  // Read 3 sink PDOs from DPM registers
  unsigned char data[12];
  int           i, j = 0;
  stusb_read(STUSB_DPM_SNK_PDO1, data, 12);
  for (i = 0; i < 3; i++) {
    sinkPDO[i].d32 = (uint32_t)(data[j] + (data[j + 1] << 8) + (data[j + 2] << 16) + (data[j + 3] << 24));
    j += 4;
  }

  clear_alerts();
}

void STUSB4500::check_negotiation() {
  switch (negotiationState) {
  case NegotiationState::NotStarted:
    if (!is_attached())
      return;
    negotiationState = NegotiationState::GettingCapabilities;
  // wait until the last moment for caps to arrive
  // (no explicit timeout here — check_negotiation is called periodically)
  case NegotiationState::GettingCapabilities:
    wait_sink_ready();
    if (get_source_capabilities()) {
      negotiationState = NegotiationState::HaveCapabilities;
    } else {
      break;
    }
  case NegotiationState::HaveCapabilities: {
    uint16_t vmax_mV = USB_PD_VMAX * 1000;
    int      bestIdx = -1, best_mV = 0, best_mA = 0;
    int      bestPower = 0;

    for (uint8_t i = 0; i < srcPDOCount; i++) {
      if (srcPDO[i].fix.FixedSupply != 0)
        continue;

      int mV = srcPDO[i].fix.Voltage * 50;
      int mA = srcPDO[i].fix.Max_Operating_Current * 10;

      if (mV > (int)vmax_mV)
        continue;

      int power = mV * mA;
      if (power > bestPower) {
        bestPower = power;
        best_mV   = mV;
        best_mA   = mA;
        bestIdx   = i;
      }
    }

    if (bestIdx < 0) {
      negotiationState = NegotiationState::Done;
      break;
    }

    wait_sink_ready();
    update_PDO(2, best_mV, best_mA);
    uint8_t n = 2;
    stusb_write(STUSB_DPM_PDO_NUMB, &n, 1);
    vTaskDelay(pdMS_TO_TICKS(1000));
    send_soft_reset();

    powerSupplyWattageLimit = ((best_mV * best_mA) / 1000000) - 2; // Take off 2W for safety of overhead
    negotiationState        = NegotiationState::Negotiating;
  }
  case NegotiationState::Negotiating: {
    wait_sink_ready();
    negotiationState = NegotiationState::Done;
  }
  case NegotiationState::Done:
    break;
  }
}

// Check if cable attached
bool STUSB4500::is_attached() {
  uint8_t status;
  if (!stusb_read(STUSB_REG_PORT_STATUS, &status, 1))
    return false;
  return (status & STUSB_MASK_ATTACHED_STATUS) == STUSB_VALUE_ATTACHED;
}

// Sink send "Soft_reset" message to Source:
// Set TX_header to soft reset: @x51 = x0D
// Send PD_command: @x1A = x26
bool STUSB4500::send_soft_reset() {
  if (!is_attached())
    return false;
  uint16_t data16 = STUSB_PD_HEADER_SOFTRESET;
  uint8_t  data8[2];
  data8[0] = data16 & 0xFF;
  data8[1] = (data16 >> 8) & 0xFF;
  if (!stusb_write(STUSB_TX_HEADER, (uint8_t *)&data8, 2))
    return false;
  uint8_t cmd = 0x26;
  if (!stusb_write(STUSB_CMD_CTRL, &cmd, 1))
    return false;
  return true;
}

bool STUSB4500::update_PDO(uint8_t PDO_number, int voltage_mV, int current_mA) {
  if (PDO_number < 1 || PDO_number > 3)
    return false;
  uint8_t PDO_index                           = PDO_number - 1;
  sinkPDO[PDO_index].fix.Operationnal_Current = current_mA / 10;
  if (PDO_number == 1) {
    // force 5V for PDO_1 to follow the USB PD spec
    sinkPDO[PDO_index].fix.Voltage = 100; // 5000/50=100
  } else {
    sinkPDO[PDO_index].fix.Voltage = voltage_mV / 50;
  }
  uint8_t address = STUSB_DPM_SNK_PDO1 + 4 * PDO_index;
  return stusb_write(address, (uint8_t *)&sinkPDO[PDO_index].d32, 4);
}

// Send soft reset then agressively poll for RX message
bool STUSB4500::get_source_capabilities() {
  if (srcPDOCount > 0)
    return true; // PD source hotplug is not supposed so early return if already done
  if (!send_soft_reset())
    return false;
  TickType_t start = xTaskGetTickCount();
  while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(50)) {
    // Check the RX header
    uint8_t RX_header[2];
    if (!stusb_read(STUSB_RX_HEADER, RX_header, 2))
      continue;
    uint16_t hdr    = RX_header[0] | (RX_header[1] << 8);
    uint8_t  msgTyp = (hdr & 0x1F);
    uint8_t  numObj = (hdr >> 12) & 0x7;
    if (msgTyp != STUSBPD_DATAMSG_Source_Capabilities || numObj == 0 || numObj > 7)
      continue;
    // Source capabilities received
    uint8_t buf[28];
    if (!stusb_read(STUSB_RX_DATA_OBJ, buf, numObj * 4))
      return false;
    srcPDOCount = numObj;
    for (uint8_t i = 0; i < numObj; i++) {
      srcPDO[i].d32 = (uint32_t)(buf[i * 4] | (buf[i * 4 + 1] << 8) | (buf[i * 4 + 2] << 16) | (buf[i * 4 + 3] << 24));
    }
    return true;
  }
  return false; // Timed out
}

// This function resets STUSB45 type-C and USB PD state machines. It also clears any
// ALERT. By initialisating Type-C pull-down termination, it forces electrical USB type-C
// disconnection (both on SOURCE and SINK sides).
bool STUSB4500::reset_by_reg() {
  uint8_t val = 1; // SW_RESET_EN = 1
  if (!stusb_write(STUSB_RESET_CTRL_REG, &val, 1))
    return false;
  // Wait for device to recover
  for (int i = 0; i < 5; i++) {
    uint8_t id;
    stusb_read(STUSB_REG_DEVICE_ID, &id, 1);
  }
  clear_alerts();
  vTaskDelay(pdMS_TO_TICKS(30)); // Type-C debounce
  // Clear reset bit
  val = 0;
  stusb_write(STUSB_RESET_CTRL_REG, &val, 1);
  return true;
}

void STUSB4500::clear_alerts() {
  // Clear all alert/status registers (read-clear)
  uint8_t dummy;
  for (uint8_t reg = STUSB_ALERT_STATUS_1; reg <= STUSB_ALERT_STATUS_1 + 12; reg++) {
    stusb_read(reg, &dummy, 1);
  }
}

bool STUSB4500::wait_sink_ready() {
  TickType_t start = xTaskGetTickCount();
  while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(200)) {
    uint8_t pe;
    if (stusb_read(STUSB_PE_FSM_STATE, &pe, 1) && pe == STUSB_PE_SNK_READY)
      return true;
    vTaskDelay(5);
  }
  return false;
}

bool STUSB4500::has_run_selection() { return negotiationState != NegotiationState::NotStarted; }
bool STUSB4500::has_negotiated() { return negotiationState == NegotiationState::Done; }

stusb_debug_state_t STUSB4500::debug_get_state() {
  stusb_debug_state_t state = {};
  state.pdo_num             = srcPDOCount;
  state.attached            = is_attached(); // reads PORT_STATUS_1 @0x0E bit 0

  stusb_read(STUSB_PE_FSM_STATE, &state.pe_state, 1); // @0x29

  STUSB_MONITORING_STATUS_RegTypeDef monitoring = {0};
  if (stusb_read(STUSB_TYPEC_MONITORING_STATUS_1, &monitoring.d8, 1)) {
    state.vbus_ready = monitoring.b.VBUS_READY; // @0x10 bit 3
  }

  return state;
}

// Returns pointer to the cached source capabilities and sets *count to their number.
const STUSB_PD_SRC_PDO_TypeDef *STUSB4500::get_last_seen_capabilities(uint8_t *count) {
  if (count)
    *count = srcPDOCount;
  return srcPDO;
}

#endif