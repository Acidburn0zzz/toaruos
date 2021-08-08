/**
 * @brief AHCI Block Device Driver
 * @file modules/ahci.c
 *
 */
#include <kernel/syscall.h>
#include <kernel/module.h>
#include <kernel/printf.h>
#include <kernel/pci.h>
#include <kernel/vfs.h>
#include <kernel/mmu.h>

static uint32_t mmio_read4(uintptr_t mmiobase, intptr_t offset) {
	volatile uint32_t * data = (volatile uint32_t *)(mmiobase + offset);
	return *data;
}

static void find_ahci(uint32_t device, uint16_t vendorid, uint16_t deviceid, void * extra) {
	if (pci_find_type(device) != 0x0106) return; /* Mass Storage, SATA controller */
	if (pci_read_field(device, PCI_PROG_IF, 1) != 0x01) return; /* AHCI */
	fs_node_t * stderr = extra;

	fprintf(stderr, "ahci: located device at %#x\n", device);

	uint16_t command_reg = pci_read_field(device, PCI_COMMAND, 2);
	command_reg |= (1 << 2);
	command_reg |= (1 << 1);
	command_reg ^= (1 << 10);
	pci_write_field(device, PCI_COMMAND, 2, command_reg);

	fprintf(stderr, "ahci: examining PCI config space...\n");
	fprintf(stderr, "ahci: interrupt line = %d\n", pci_get_interrupt(device));
	fprintf(stderr, "ahci: BAR5 = %#x\n", pci_read_field(device, PCI_BAR5, 4));

	uintptr_t mmio_addr = (uintptr_t)mmu_map_mmio_region(pci_read_field(device, PCI_BAR5, 4) & 0xFFFFFFF0, 0x2000); /* I have no idea how much space this needs */
	fprintf(stderr, "ahci: mapping mmio to %#zx\n", mmio_addr);

	uint32_t enabledPorts = mmio_read4(mmio_addr, 0x0C);
	fprintf(stderr, "ahci: implemented ports = %#x\n", enabledPorts);

	uint32_t ahciVersion = mmio_read4(mmio_addr, 0x10);
	fprintf(stderr, "ahci: version %d.%d%d\n",
		(ahciVersion >> 16) & 0xFFF,
		(ahciVersion >> 8) & 0xFF,
		(ahciVersion) & 0xFF);

	int offset = 0x100;
	for (int port = 0; port < 32; ++port) {
		if (enabledPorts & (1UL << port)) {
			/* Check status */
			uint32_t portSig    = mmio_read4(mmio_addr, offset + 0x24);
			uint32_t portStatus = mmio_read4(mmio_addr, offset + 0x28);
			fprintf(stderr, "ahci: port %d: status = %#x\n", port, portStatus);
			fprintf(stderr, "ahci: port %d: sig    = %#x\n", port, portSig);

			switch (portSig) {
				case 0xeb140101:
					fprintf(stderr, "ahci:           ATAPI (CD, DVD)\n");
					break;
				case 0x00000101:
					fprintf(stderr, "ahci:           hard disk\n");
					break;
				case 0xffff0101:
					fprintf(stderr, "ahci:           no device\n");
					break;
				default:
					fprintf(stderr, "ahci:           unsupported/unknown\n");
					break;
			}

		}
		offset += 0x80;
	}


	
}

static int init(int argc, char * argv[]) {
	fs_node_t * node = FD_ENTRY(1); /* Get the stdout for the process that loaded the module */
	pci_scan(find_ahci, -1, node);
	return 0;
}

static int fini(void) {
	return 0;
}

struct Module metadata = {
	.name = "ahci",
	.init = init,
	.fini = fini,
};

