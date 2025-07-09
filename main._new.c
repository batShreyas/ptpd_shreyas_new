#include <lwip/init.h>
#include <lwip/netif.h>
#include <netif/xemacpsif.h>
#include <lwip/tcpip.h>
#include <lwip/timeouts.h>
#include <xil_printf.h>
#include <xparameters.h>
#include <xil_cache.h>
#include <xscugic.h>
#include <xil_exception.h>
#include <xuartps.h>
#include <xtmrctr.h>
#include <stdbool.h>

#include "ptpd.h"   // your modified ptpd API header

// Global PTP variables
sys_mbox_t ptp_alert_queue;
ptpd_opts opts;
ptp_clock_t ptp_clock;
foreign_master_record_t foreign_records[PTPD_DEFAULT_MAX_FOREIGN_RECORDS];
struct netif lwip_netif;

/* Platform initialization: caches, UART, timer, GIC */
void platform_init() {
    // Enable caches
    Xil_ICacheEnable();
    Xil_DCacheEnable();

    // UART init (optional debug)
    XUartPs Uart;
    XUartPs_Config *Config = XUartPs_LookupConfig(XPAR_XUARTPS_0_DEVICE_ID);
    if (Config)
        XUartPs_CfgInitialize(&Uart, Config, Config->BaseAddress);

    // Timer init (for lwIP timeouts)
    XTmrCtr Timer;
    XTmrCtr_Initialize(&Timer, XPAR_TMRCTR_0_DEVICE_ID);
    XTmrCtr_Start(&Timer, 0);

    // Interrupt controller
    XScuGic Intc;
    XScuGic_Config *GicConfig = XScuGic_LookupConfig(XPAR_SCUGIC_0_DEVICE_ID);
    XScuGic_CfgInitialize(&Intc, GicConfig, GicConfig->CpuBaseAddress);
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
                                 (Xil_ExceptionHandler)XScuGic_InterruptHandler,
                                 &Intc);
    Xil_ExceptionEnable();

    xil_printf("Platform initialized.\n");
}

/* Initialize ptpd options and clock */
void ptpd_opts_init() {
    if (ptp_startup(&ptp_clock, &opts, foreign_records) != 0) {
        xil_printf("ptpd: startup failed\n");
        return;
    }

#ifdef USE_DHCP
    // Wait for IP from DHCP
    while (!netif_default->ip_addr.addr)
        sys_msleep(500);
#endif
}

int main(void) {
    platform_init();         // Init platform (UART, timer, GIC)
    lwip_init();             // Init lwIP stack

    // Set static IP, mask, gateway
    ip_addr_t ipaddr, netmask, gw;
    IP4_ADDR(&ipaddr, 192, 168, 1, 100);
    IP4_ADDR(&netmask, 255, 255, 255, 0);
    IP4_ADDR(&gw, 192, 168, 1, 1);

    // Add network interface
    netif_add(&lwip_netif, &ipaddr, &netmask, &gw, NULL, xemacpsif_init, tcpip_input);
    netif_set_default(&lwip_netif);
    netif_set_up(&lwip_netif);

    // Init PTP alert queue
    ptp_alert_queue = sys_mbox_new();

    // Initialize PTP options and clock
    ptpd_opts_init();

    xil_printf("PTPd started on IP: %s\n", ipaddr_ntoa(&lwip_netif.ip_addr));

    while (1) {
        // lwIP timers (ARP, DHCP, etc.)
        sys_check_timeouts();

        // Call ptpd FSM handler
        ptpd_periodic_handler();

        // Sleep to yield CPU
        sys_msleep(10);
    }

    return 0;
}
