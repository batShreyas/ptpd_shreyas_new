Excellent debugging! That tells us exactly where the problem is.

The program is crashing inside this function call:
xemac_add(netif, NULL, NULL, NULL, mac_ethernet_address, PLATFORM_EMAC_BASEADDR)

This function is responsible for initializing the actual Ethernet MAC hardware on the FPGA. A crash here almost always means there is a mismatch between your hardware design in Vivado and your software platform in Vitis.

The software is trying to talk to the Ethernet hardware at a specific memory address (PLATFORM_EMAC_BASEADDR), but the hardware isn't there or isn't configured as the software expects.

The Solution: Re-sync Vivado and Vitis
You need to force Vitis to update its knowledge of your hardware design.

Go back to Vivado:

Open your completed block design.

From the menu, select File -> Export -> Export Hardware.

Export the hardware platform, making sure to include the bitstream. Save the resulting .xsa file.

Go back to Vitis:

In the "Explorer" panel on the left, find your Platform Project (it will have a little platform icon).

Right-click on the platform project and select Update Hardware Specification.

In the dialog box, point it to the new .xsa file you just exported from Vivado.

Vitis will now update the platform.

Re-build the Platform and Application:

After the hardware specification is updated, right-click on your Platform Project again and select Build Project. This will regenerate the Board Support Package (BSP) with the correct hardware information.

Once the platform is done building, right-click on your Application Project and select Build Project.

Re-run on Hardware:

Now, run the application on the hardware again.

This process ensures that your software has the correct, up-to-date information about your FPGA design. This should fix the crash in xemac_add().





#include "xparameters.h"
#include "netif/xadapter.h"
#include "platform.h"
#include "xil_printf.h"
#include "xil_cache.h"

// lwIP Includes
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/inet.h"

// --- Constant Definitions ---
#define DEFAULT_IP_ADDRESS  "192.168.0.10"
#define DEFAULT_IP_MASK     "255.255.255.0"
#define DEFAULT_GW_ADDRESS  "192.168.0.1"

// --- Global Variables ---
struct netif server_netif;

// --- Function Prototypes ---
static void assign_default_ip(ip_addr_t *ip, ip_addr_t *mask, ip_addr_t *gw);

int main()
{
    struct netif *netif = &server_netif;
    unsigned char mac_ethernet_address[] = { 0x00, 0x0a, 0x35, 0x00, 0x01, 0x02 };

    // 1. Initialize the platform (for UART)
    init_platform();
    xil_printf("--- Minimal lwIP Ping Test ---\r\n");

    // 2. Initialize the lwIP stack
    lwip_init();
    xil_printf("lwIP Initialized.\r\n");

    // 3. Add and configure the network interface
    if (!xemac_add(netif, NULL, NULL, NULL, mac_ethernet_address, PLATFORM_EMAC_BASEADDR)) {
        xil_printf("Error adding network interface\r\n");
        return -1;
    }
    netif_set_default(netif);
    netif_set_up(netif);
    xil_printf("Network Interface is Up.\r\n");

    // 4. Assign the static IP address
    assign_default_ip(&(netif->ip_addr), &(netif->netmask), &(netif->gw));
    xil_printf("IP Address Assigned: %s\r\n", ip4addr_ntoa(&(netif->ip_addr)));
    xil_printf("Ready to be pinged.\r\n");

    // 5. --- Main Polling Loop ---
    // This loop does nothing but check for incoming network packets.
    while (1) {
        xemacif_input(netif);
    }

    cleanup_platform();
    return 0;
}

// --- Helper function to assign the IP address ---
static void assign_default_ip(ip_addr_t *ip, ip_addr_t *mask, ip_addr_t *gw) {
    inet_aton(DEFAULT_IP_ADDRESS, ip);
    inet_aton(DEFAULT_IP_MASK, mask);
    inet_aton(DEFAULT_GW_ADDRESS, gw);
}


