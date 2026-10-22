/** The thronectl GATT service. RX: the host writes without response. TX: the device notifies the byte stream. */
export const BLE_SERVICE = "7b3e0001-6f1a-4c2e-9d6a-2f1c8e5a7d10";
export const BLE_RX = "7b3e0002-6f1a-4c2e-9d6a-2f1c8e5a7d10";
export const BLE_TX = "7b3e0003-6f1a-4c2e-9d6a-2f1c8e5a7d10";
/** Read and notify: u32 LE bytes the device has read from RX so far, then u16 LE ATT MTU. */
export const BLE_CREDIT = "7b3e0004-6f1a-4c2e-9d6a-2f1c8e5a7d10";
/** Manufacturer data in the scan response: company id 0xFFFF (reserved for tests), "TC", 8-byte device id, flags. */
export const BLE_COMPANY = 0xffff;
export const BLE_FLAG_PAIRING = 0x01;
