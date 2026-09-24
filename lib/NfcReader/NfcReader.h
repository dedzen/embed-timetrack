#pragma once

// Call after battery_gauge_init() has configured the shared Wire pins.
void nfc_reader_init();
// Enable NFC scanning while the main screen is active.
bool nfc_reader_start();
// Run only while the main screen is active.
void nfc_reader_update();
// Hold PN532 reset low when leaving the main screen or sleeping.
void nfc_reader_stop();
