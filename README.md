**ATLANTIC Projet**

This project aim to use the **ESP32-C3 3 Wire Lin Transceiver Board,** developped by _@nathansags_ and available on eBay Australia, to control my Altlantic/Fujitsu heat pump.

The resulting firmware shall integrate in the Apple Home Kit ecosystem.

**Note:** Since the LIN communication is connected to the regular UART0\_RX, UART0\_TX pins, the default Serial is redirected to USB. To prevent BOOT information to be sent to the LIN interface, the _eFuse_ **UART\_PRINT\_CONTROL 3** has been burnt so that nothing is sent out to LIN upon reboot.