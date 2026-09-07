#pragma once

#ifndef _DRIVERS_STUSB4500_HPP_
#define _DRIVERS_STUSB4500_HPP_
#include "configuration.h"
#ifdef POW_PD_STUSB4500

#define STUSB4500_ADDR (0x28 << 1) // I2C address: 7-bit 0x28, stored in 8-bit write-address format

// Identification of STUSB
#define STUSB_REG_DEVICE_ID 0x2F // Device ID register address
#define STUSB4500_ID        0x25
#define STUSB4500_ID_B      0x21

// Reference: https://github.com/usb-c/STUSB4500
#define STUSB_ALERT_STATUS_1            0x0B // Interrupt register
#define STUSB_DPM_SNK_PDO1              0x85 // Sink PDO
#define STUSB_REG_PORT_STATUS           0x0E // Status of the connection detection
#define STUSB_MASK_ATTACHED_STATUS      0x01
#define STUSB_VALUE_NOT_ATTACHED        0
#define STUSB_VALUE_ATTACHED            1
#define STUSB_PD_HEADER_SOFTRESET       0x000D
#define STUSB_TX_HEADER                 0x51 // 16bit
#define STUSB_CMD_CTRL                  0x1A
#define STUSB_DPM_PDO_NUMB              0x70
#define STUSB_RESET_CTRL_REG            0x23 // Allows to reset the device by software
#define STUSB_TYPEC_MONITORING_STATUS_1 0x10 // Provides information on current status of the VBUS and VCONN voltages

#define STUSBPD_DATAMSG_Source_Capabilities 0x01

// RX
#define STUSB_RX_BYTE_CNT 0x30
#define STUSB_RX_HEADER   0x31 // RX message header (16bit)
#define STUSB_RX_DATA_OBJ 0x33 // (32bit)

#define STUSB_PE_FSM_STATE                 0x29 // Policy engine layer FSM state
#define STUSB_PE_INIT                      0x00
#define STUSB_PE_SOFT_RESET                0x01
#define STUSB_PE_HARD_RESET                0x02
#define STUSB_PE_SEND_SOFT_RESET           0x03
#define STUSB_PE_C_BIST                    0x04
#define STUSB_PE_DISABLED                  0x0D
#define STUSB_PE_SNK_STARTUP               0x12
#define STUSB_PE_SNK_DISCOVERY             0x13
#define STUSB_PE_SNK_WAIT_FOR_CAPABILITIES 0x14
#define STUSB_PE_SNK_EVALUATE_CAPABILITIES 0x15
#define STUSB_PE_SNK_SELECT_CAPABILITIES   0x16
#define STUSB_PE_SNK_TRANSITION_SINK       0x17
#define STUSB_PE_SNK_READY                 0x18
#define STUSB_PE_SNK_READY_SENDING         0x19
#define STUSB_PE_HARD_RESET_SHUTDOWN       0x3A
#define STUSB_PE_HARD_RESET_RECOVERY       0x3B
#define STUSB_PE_ERRORRECOVERY             0x40

typedef union {
  uint32_t d32;
  struct {
    uint32_t Operationnal_Current       : 10;
    uint32_t Voltage                    : 10;
    uint8_t  Reserved_22_20             : 3;
    uint8_t  Fast_Role_Req_cur          : 2; /* must be set to 0 in 2.0*/
    uint8_t  Dual_Role_Data             : 1;
    uint8_t  USB_Communications_Capable : 1;
    uint8_t  Unconstrained_Power        : 1;
    uint8_t  Higher_Capability          : 1;
    uint8_t  Dual_Role_Power            : 1;
    uint8_t  Fixed_Supply               : 2;

  } fix;
  struct {
    uint32_t Operating_Current : 10;
    uint32_t Min_Voltage       : 10;
    uint32_t Max_Voltage       : 10;
    uint8_t  VariableSupply    : 2;
  } var;
  struct {
    uint32_t Operating_Power : 10;
    uint32_t Min_Voltage     : 10;
    uint32_t Max_Voltage     : 10;
    uint8_t  Battery         : 2;
  } bat;

} STUSB_PD_SNK_PDO_TypeDef;

typedef union {
  uint32_t d32;
  struct {
    // Table 6-9 Fixed Supply PDO - Source
    uint32_t Max_Operating_Current : 10; // Bits 9..0
    uint32_t Voltage               : 10; // Bits 19..10
    uint8_t  PeakCurrent           : 2;  // Bits 21..20
    uint8_t  Reserved              : 3;
    uint8_t  DataRoleSwap          : 1; // Bits 25
    uint8_t  Communication         : 1; // Bits 26
    uint8_t  ExternalyPowered      : 1; // Bits 27
    uint8_t  SuspendSuported       : 1; // Bits 28
    uint8_t  DualRolePower         : 1; // Bits 29
    uint8_t  FixedSupply           : 2; // Bits 31..30
  } fix;

  struct {
    // Table 6-11 Variable Supply (non-Battery) PDO - Source
    uint32_t Operating_Current : 10;
    uint32_t Min_Voltage       : 10;
    uint32_t Max_Voltage       : 10;
    uint8_t  VariableSupply    : 2;
  } var;

  struct {
    // Table 6-12 Battery Supply PDO - Source
    uint32_t Operating_Power : 10;
    uint32_t Min_Voltage     : 10;
    uint32_t Max_Voltage     : 10;
    uint8_t  Battery         : 2;
  } bat;

} STUSB_PD_SRC_PDO_TypeDef;

typedef union {
  uint8_t d8;
  struct {
    uint8_t VCONN_VALID    : 1;
    uint8_t VBUS_VALID_SNK : 1;
    uint8_t VBUS_VSAFE0V   : 1;
    uint8_t VBUS_READY     : 1;
    uint8_t _Reserved_4_7  : 4;
  } b;
} STUSB_MONITORING_STATUS_RegTypeDef;

typedef struct {
  uint8_t attached;   // cable attached (PORT_STATUS_1 bit 0, @0x0E)
  uint8_t pdo_num;    // source PDO count
  uint8_t pe_state;   // raw PE_FSM_STATE @0x29
  uint8_t vbus_ready; // 1 = VBUS within valid range (@0x10, VBUS_READY bit)
} stusb_debug_state_t;

class STUSB4500 {
public:
  static void                            init();
  static bool                            probe();
  static bool                            is_attached();
  static void                            check_negotiation();
  static bool                            has_negotiated();
  static bool                            has_run_selection();
  static const STUSB_PD_SRC_PDO_TypeDef *get_last_seen_capabilities(uint8_t *count);
  static stusb_debug_state_t             debug_get_state();
  static bool                            is_vbus_ready();
  static bool                            negotiate();

private:
  static bool update_PDO(uint8_t PDO_number, int voltage_mV, int current_mA);
  static bool send_soft_reset();
  static bool reset_by_reg();
  static bool get_source_capabilities();
  static void clear_alerts();
  static bool wait_sink_ready();
};

#endif // POW_PD_STUSB4500
#endif