/*	$NetBSD: i82365_isa.c,v 1.20 2002/10/02 03:10:47 thorpej Exp $	*/

/*
 * Copyright (c) 1997 Marc Horowitz.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Marc Horowitz.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>

#define	PCICISADEBUG

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/extent.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/core/isa/isareg.h>
#include <dev/core/isa/isavar.h>

#include <dev/core/pcmcia/pcmciareg.h>
#include <dev/core/pcmcia/pcmciavar.h>
#include <dev/core/pcmcia/pcmciachip.h>

#include <dev/core/io/i82365/i82365reg.h>
#include <dev/core/io/i82365/i82365var.h>
#include <dev/core/io/i82365/i82365_isavar.h>

#ifdef PCICISADEBUG
int	pcicisa_debug = 0 /* XXX */ ;
#define	DPRINTF(arg) if (pcicisa_debug) printf arg;
#else
#define	DPRINTF(arg)
#endif

int		pcic_isa_probe(struct device *, struct cfdata *, void *);
void	pcic_isa_attach(struct device *, struct device *, void *);

extern struct cfdriver pcic_cd;
CFOPS_DECL(pcic_isa, pcic_isa_probe, pcic_isa_attach, NULL, NULL);
CFATTACH_DECL(pcic_isa, &pcic_cd, &pcic_isa_cops, sizeof(struct pcic_isa_softc));

static struct pcmcia_chip_functions pcic_isa_functions = {
	pcic_chip_mem_alloc,
	pcic_chip_mem_free,
	pcic_chip_mem_map,
	pcic_chip_mem_unmap,

	pcic_chip_io_alloc,
	pcic_chip_io_free,
	pcic_chip_io_map,
	pcic_chip_io_unmap,

	pcic_isa_chip_intr_establish,
	pcic_isa_chip_intr_disestablish,

	pcic_chip_socket_enable,
	pcic_chip_socket_disable,
};

int
pcic_isa_probe(parent, match, aux)
	struct device *parent;
	struct cfdata *match;
	void *aux;
{
	struct isa_attach_args *ia = aux;
	bus_space_tag_t iot = ia->ia_iot;
	bus_space_handle_t ioh, memh;
	int val, found;

	if (ia->ia_nio < 1) {
		return 0;
	}
	if (ia->ia_niomem < 1) {
		return 0;
	}

	if (ISA_DIRECT_CONFIG(ia))
		return 0;

	/* Disallow wildcarded i/o address. */
	if (ia->ia_iobase == IOBASEUNK)
		return (0);
	if(ia->ia_maddr == MADDRUNK) {
		return (0);
	}

	if (bus_space_map(iot, ia->ia_iobase, PCIC_IOSIZE, 0, &ioh))
		return (0);

	if (ia->ia_msize == -1)
		ia->ia_msize = PCIC_MEMSIZE;

	if (bus_space_map(ia->ia_memt, ia->ia_maddr, ia->ia_msize, 0, &memh))
		bus_space_unmap(iot, ioh, PCIC_IOSIZE);
		return (0);

	found = 0;

	/*
	 * this could be done with a loop, but it would violate the
	 * abstraction
	 */

	bus_space_write_1(iot, ioh, PCIC_REG_INDEX, C0SA + PCIC_IDENT);

	val = bus_space_read_1(iot, ioh, PCIC_REG_DATA);

	if (pcic_ident_ok(val))
		found++;


	bus_space_write_1(iot, ioh, PCIC_REG_INDEX, C0SB + PCIC_IDENT);

	val = bus_space_read_1(iot, ioh, PCIC_REG_DATA);

	if (pcic_ident_ok(val))
		found++;


	bus_space_write_1(iot, ioh, PCIC_REG_INDEX, C1SA + PCIC_IDENT);

	val = bus_space_read_1(iot, ioh, PCIC_REG_DATA);

	if (pcic_ident_ok(val))
		found++;


	bus_space_write_1(iot, ioh, PCIC_REG_INDEX, C1SB + PCIC_IDENT);

	val = bus_space_read_1(iot, ioh, PCIC_REG_DATA);

	if (pcic_ident_ok(val))
		found++;


	bus_space_unmap(iot, ioh, PCIC_IOSIZE);
	bus_space_unmap(ia->ia_memt, memh, ia->ia_msize);

	if (!found)
		return (0);

	ia->ia_nio = 1;
	ia->ia_iosize = PCIC_IOSIZE;

	ia->ia_niomem = 1;

	/* IRQ is special. */

	ia->ia_ndrq = 0;

	return (1);
}

void
pcic_isa_attach(parent, self, aux)
	struct device *parent, *self;
	void *aux;
{
	struct pcic_softc *sc = (void *) self;
	struct pcic_isa_softc *isc = (void *) self;
	struct isa_attach_args *ia = aux;
	isa_chipset_tag_t ic = ia->ia_ic;
	bus_space_tag_t iot = ia->ia_iot;
	bus_space_tag_t memt = ia->ia_memt;
	bus_space_handle_t ioh;
	bus_space_handle_t memh;

	/* Map i/o space. */
	if (bus_space_map(iot, ia->ia_iobase, PCIC_IOSIZE, 0, &ioh)) {
		printf(": can't map i/o space\n");
		return;
	}

	/* Map mem space. */
	if (bus_space_map(memt, ia->ia_maddr, ia->ia_msize, 0, &memh)) {
		printf(": can't map mem space\n");
		return;
	}

	sc->membase = ia->ia_maddr;
	sc->subregionmask = (1 << (ia->ia_msize / PCIC_MEM_PAGESIZE)) - 1;

	isc->sc_ic = ic;
	sc->pct = (pcmcia_chipset_tag_t) & pcic_isa_functions;

	sc->iot = iot;
	sc->ioh = ioh;
	sc->memt = memt;
	sc->memh = memh;
	if (ia->ia_nirq > 0) {
		sc->irq = ia->ia_irq;
	} else {
		sc->irq = IRQUNK;
	}
	printf("\n");

	pcic_attach(sc);
	pcic_isa_bus_width_probe(sc, iot, ioh, ia->ia_iobase, PCIC_IOSIZE);
	pcic_attach_sockets(sc);

	config_interrupts(self, pcic_isa_config_interrupts);
}
