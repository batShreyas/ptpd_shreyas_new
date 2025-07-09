#include "xparameters.h"
#include "xil_cache.h"
#include "xil_printf.h"
#include "xil_io.h"
#include "xuartps.h"
#include "xtmrctr.h"
#include "xscugic.h"
#include "netif/xemacpsif.h"
#include "platform.h"

void platform_init() {
    // 1. Enable caches
    Xil_ICacheEnable();
    Xil_DCacheEnable();

    // 2. Initialize UART (for debug prints)
    XUartPs Uart;
    XUartPs_Config *UartConfig = XUartPs_LookupConfig(XPAR_XUARTPS_0_DEVICE_ID);
    XUartPs_CfgInitialize(&Uart, UartConfig, UartConfig->BaseAddress);

    // 3. Initialize timer (optional, for lwIP or delay)
    XTmrCtr Timer;
    XTmrCtr_Initialize(&Timer, XPAR_TMRCTR_0_DEVICE_ID);
    XTmrCtr_Start(&Timer, 0);

    // 4. Initialize interrupts (if needed)
#ifdef XPAR_SCUGIC_0_DEVICE_ID
    XScuGic Gic;
    XScuGic_Config *GicConfig = XScuGic_LookupConfig(XPAR_SCUGIC_0_DEVICE_ID);
    XScuGic_CfgInitialize(&Gic, GicConfig, GicConfig->CpuBaseAddress);
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT, (Xil_ExceptionHandler)XScuGic_InterruptHandler, &Gic);
    Xil_ExceptionEnable();
#endif

    // 5. SFP/PHY Configuration (optional)
    // Usually handled by MDIO auto-negotiation or custom SFP setup code.
    // Ensure link is up and MAC is clocked.
    // You may need to set PHY registers using MDIO here.

    xil_printf("Platform initialized.\n");
}
