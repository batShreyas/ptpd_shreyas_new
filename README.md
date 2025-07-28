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






Video


