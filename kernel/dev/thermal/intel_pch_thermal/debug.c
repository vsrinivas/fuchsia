// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#if WITH_LIB_CONSOLE

#include <err.h>
#include <stdio.h>
#include <string.h>

#include <lib/console.h>
#include "pch_thermal.h"

static int cmd_pchtherm_regs(int argc, const cmd_args *argv)
{
    if (g_pch_thermal_context.regs == NULL) {
        printf("No device found\n");
        return NO_ERROR;
    }

    uint16_t raw_temp = g_pch_thermal_context.regs->temp;
    int whole_temp = decode_temp(raw_temp);
    int frac_temp = 5 * (raw_temp & 1);
    printf("TEMP:   %#02x (%d.%d C)\n", raw_temp, whole_temp, frac_temp);
    printf("TSC:    %#02x\n", g_pch_thermal_context.regs->tsc);
    printf("TSS:    %#02x\n", g_pch_thermal_context.regs->tss);
    printf("TSEL:   %#02x\n", g_pch_thermal_context.regs->tsel);
    printf("TSREL:  %#02x\n", g_pch_thermal_context.regs->tsrel);
    printf("TSMIC:  %#02x\n", g_pch_thermal_context.regs->tsmic);
    printf("CTT:    %#02x\n", g_pch_thermal_context.regs->ctt);
    printf("TAHV:   %#04x\n", g_pch_thermal_context.regs->tahv);
    printf("TALV:   %#04x\n", g_pch_thermal_context.regs->talv);
    printf("TSPM:   %#04x\n", g_pch_thermal_context.regs->tspm);
    printf("TL:     %#08x\n", g_pch_thermal_context.regs->tl);
    printf("TL2:    %#08x\n", g_pch_thermal_context.regs->tl2);
    printf("PHL:    %#04x\n", g_pch_thermal_context.regs->phl);
    printf("PHLC:   %#02x\n", g_pch_thermal_context.regs->phlc);
    printf("TAS:    %#02x\n", g_pch_thermal_context.regs->tas);
    printf("TSPIEN: %#02x\n", g_pch_thermal_context.regs->tspien);
    printf("TSGPEN: %#02x\n", g_pch_thermal_context.regs->tsgpen);
    return NO_ERROR;
}

static int cmd_pchtherm(int argc, const cmd_args *argv)
{
    static const struct {
        const char* name;
        int (*subcmd)(int, const cmd_args*);
    } SUBCMDS[] = {
        { "regs",   cmd_pchtherm_regs },
    };

    if (argc >= 2) {
        for (size_t i = 0; i < countof(SUBCMDS); ++i)
            if (!strcmp(argv[1].str, SUBCMDS[i].name))
                return SUBCMDS[i].subcmd(argc, argv);
    }

    printf("usage: %s <cmd> [args]\n"
           "Valid cmds are...\n"
           "\thelp   : Show this message\n"
           "\tregs   : Dump the registers for the device, if present\n",
           argv[0].str);

    return NO_ERROR;
}

STATIC_COMMAND_START
STATIC_COMMAND("pchtherm",
               "Low level commands to manipulate the Intel PCH Thermal Sensors device",
                &cmd_pchtherm)
STATIC_COMMAND_END(intel_pch_thermal_commands);

#endif
