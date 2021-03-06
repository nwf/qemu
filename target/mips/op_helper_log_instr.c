/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2015-2016 Stacey Son <sson@FreeBSD.org>
 * Copyright (c) 2016-2018 Alfredo Mazzinghi <am2419@cl.cam.ac.uk>
 * Copyright (c) 2016-2018 Alex Richardson <Alexander.Richardson@cl.cam.ac.uk>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
 * ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "qemu/osdep.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "exec/helper-proto.h"
#include "exec/log.h"
#include "cpu.h"
#include "internal.h"

#ifdef CONFIG_MIPS_LOG_INSTR

extern int cl_default_trace_format;


#define USER_TRACE_DEBUG 0
#if USER_TRACE_DEBUG
#define user_trace_dbg(...) qemu_log("=== " __VA_ARGS__)
#else
#define user_trace_dbg(...)
#endif

/* Start instruction trace logging. */
void helper_instr_start(CPUMIPSState *env, target_ulong pc)
{
    env->trace_explicitly_disabled = false;
    /* Don't turn on tracing if user-mode only is selected and we are in the kernel */
    if (env->user_only_tracing_enabled && !IN_USERSPACE(env)) {
        assert(qemu_loglevel_mask(CPU_LOG_USER_ONLY));
        user_trace_dbg("Delaying tracing request at 0x%lx "
            "until next switch to user mode, ASID %lu\n",
            pc, env->CP0_EntryHi & 0xFF);
        env->tracing_suspended = true;
    } else {
        qemu_set_log(qemu_loglevel | cl_default_trace_format);
        user_trace_dbg("Switching on tracing @ 0x%lx ASID %lu\n",
            pc, env->CP0_EntryHi & 0xFF);
        env->tracing_suspended = false;
    }
}

/* Stop instruction trace logging. */
void helper_instr_stop(CPUMIPSState *env, target_ulong pc)
{
    user_trace_dbg("Switching off tracing @ 0x%lx ASID %lu\n",
        pc, env->CP0_EntryHi & 0xFF);
    qemu_set_log(qemu_loglevel & ~cl_default_trace_format);
    /* Make sure a kernel -> user switch does not turn on tracing */
    env->tracing_suspended = false;
    /* don't turn on on next kernel -> userspace change */
    env->trace_explicitly_disabled = true;
}

/* Set instruction trace logging to user mode only. */
void helper_instr_start_user_mode_only(CPUMIPSState *env, target_ulong pc)
{
    /*
     * Make sure that qemu_loglevel doesn't get set to zero when we
     * suspend tracing because otherwise qemu will close the logfile.
     */
    qemu_set_log(qemu_loglevel | CPU_LOG_USER_ONLY);
    user_trace_dbg("User-mode only tracing enabled at 0x%lx, ASID %lu\n",
        pc, env->CP0_EntryHi & 0xFF);
    env->user_only_tracing_enabled = true;
    /* Disable tracing if we are not currently in user mode */
    if (!IN_USERSPACE(env)) {
        qemu_set_log(qemu_loglevel & ~cl_default_trace_format);
        env->tracing_suspended = true;
    } else {
        env->tracing_suspended = false;
    }
}

/* Stop instruction trace logging to user mode only. */
void helper_instr_stop_user_mode_only(CPUMIPSState *env, target_ulong pc)
{
    /* Disable user-mode only and restore the previous tracing level */
    if (env->tracing_suspended && !env->trace_explicitly_disabled) {
        user_trace_dbg("User-only trace turned off -> Restoring old trace level"
            " at 0x%lx, ASID %lu\n", pc, env->CP0_EntryHi & 0xFF);
        qemu_set_log(qemu_loglevel | cl_default_trace_format);
    }
    env->tracing_suspended = false;
    env->user_only_tracing_enabled = false;
    user_trace_dbg("User-mode only tracing disabled at 0x%lx, ASID %lu\n",
        pc, env->CP0_EntryHi & 0xFF);
    qemu_set_log(qemu_loglevel & ~CPU_LOG_USER_ONLY);
}

void do_hexdump(FILE* f, uint8_t* buffer, target_ulong length, target_ulong vaddr) {
    char ascii_chars[17] = { 0 };
    target_ulong line_start = vaddr & ~0xf;
    target_ulong addr;

    /* print leading empty space to always start with an aligned address */
    if (line_start != vaddr) {
        fprintf(f, "    " TARGET_FMT_lx" : ", line_start);
        for (addr = line_start; addr < vaddr; addr++) {
            if ((addr % 4) == 0) {
                fprintf(f, "   ");
            } else {
                fprintf(f, "  ");
            }
            ascii_chars[addr % 16] = ' ';
        }
    }
    ascii_chars[16] = '\0';
    for (addr = vaddr; addr < vaddr + length; addr++) {
        if ((addr % 16) == 0) {
            fprintf(f, "    " TARGET_FMT_lx ": ", line_start);
        }
        if ((addr % 4) == 0) {
            fprintf(f, " ");
        }
        unsigned char c = (unsigned char)buffer[addr - vaddr];
        fprintf(f, "%02x", c);
        ascii_chars[addr % 16] = isprint(c) ? c : '.';
        if ((addr % 16) == 15) {
            fprintf(f, "  %s\r\n", ascii_chars);
            line_start += 16;
        }
    }
    if (line_start != vaddr + length) {
        const target_ulong hexdump_end_addr = (vaddr + length) | 0xf;
        for (addr = vaddr + length; addr <= hexdump_end_addr; addr++) {
            if ((addr % 4) == 0) {
                fprintf(f, "   ");
            } else {
                fprintf(f, "  ");
            }
            ascii_chars[addr % 16] = ' ';
        }
        fprintf(f, "  %s\r\n", ascii_chars);
    }
}


/* Stop instruction trace logging to user mode only. */
void helper_cheri_debug_message(struct CPUMIPSState* env, uint64_t pc)
{
    uint32_t mode = qemu_loglevel & (CPU_LOG_CVTRACE | CPU_LOG_INSTR);
    if (!mode && env->tracing_suspended) {
        /* Always print these messages even if user-space only tracing is on */
        mode = cl_default_trace_format;
    }

    if (!mode && qemu_loglevel_mask(CPU_LOG_GUEST_DEBUG_MSG))
        mode = CPU_LOG_INSTR;

    if (!mode || !qemu_log_enabled())
        return;

    uint8_t buffer[4096];
    /* Address loaded from a0, length from a1, print mode in a2 */
    typedef enum _PrintMode {
        DEBUG_MESSAGE_CSTRING = 0,
        DEBUG_MESSAGE_HEXDUMP = 1,
        DEBUG_MESSAGE_PTR = 2,
        DEBUG_MESSAGE_DECIMAL= 3
    } PrintMode;
    target_ulong vaddr = env->active_tc.gpr[4];
    target_ulong length = MIN(sizeof(buffer), env->active_tc.gpr[5]);
    PrintMode print_mode = (PrintMode)env->active_tc.gpr[6];

    // For ptr + decimal mode we only need
    if (print_mode == DEBUG_MESSAGE_PTR) {
        if (mode & CPU_LOG_INSTR) {
            qemu_log_mask(CPU_LOG_INSTR, "   ptr = 0x" TARGET_FMT_lx "\r\n", vaddr);
        }
        return;
    } else if (print_mode == DEBUG_MESSAGE_DECIMAL) {
        if (mode & CPU_LOG_INSTR) {
            qemu_log_mask(CPU_LOG_INSTR, "   value = " TARGET_FMT_ld "\r\n", vaddr);
        }
        return;
    }
    // Otherwise we meed to fetch the memory referenced by vaddr+length
    // NB: length has already been capped at sizeof(buffer)
    int ret = cpu_memory_rw_debug(env_cpu(env), vaddr, buffer, length, false);
    if (ret != 0) {
        warn_report("CHERI DEBUG HELPER: Could not read " TARGET_FMT_ld
                    " bytes at vaddr 0x" TARGET_FMT_lx "\r\n", length, vaddr);
    }
    if ((mode & CPU_LOG_INSTR) || qemu_log_enabled()) {
        qemu_log_mask(CPU_LOG_INSTR, "DEBUG MESSAGE @ 0x" TARGET_FMT_lx " addr=0x"
            TARGET_FMT_lx " len=" TARGET_FMT_ld "\r\n", pc, vaddr, length);
        if (print_mode == DEBUG_MESSAGE_CSTRING) {
            /* XXXAR: Escape newlines, etc.? */
            qemu_log_mask(CPU_LOG_INSTR, "    message = \"%*.*s\"\n", (int)length, (int)length, buffer);
        } else if (print_mode == DEBUG_MESSAGE_HEXDUMP) {
            qemu_log_mask(CPU_LOG_INSTR, "   Dumping " TARGET_FMT_lu
                     " bytes starting at 0x"
                     TARGET_FMT_lx "\r\n", length, vaddr);
            FILE* logfile = qemu_log_lock();
            do_hexdump(logfile, buffer, length, vaddr);
            qemu_log_unlock(logfile);
        }
    } else if (mode & CPU_LOG_CVTRACE) {
        warn_report("NOT IMPLEMENTED: CVTRACE debug message nop at 0x"
                    TARGET_FMT_lx "\n", pc);
    } else {
        assert(false && "logic error");
    }
}


static inline void cvtrace_dump_gpr(cvtrace_t *cvtrace, uint64_t value)
{
    if (qemu_loglevel_mask(CPU_LOG_CVTRACE)) {
        if (cvtrace->version == CVT_NO_REG)
            cvtrace->version = CVT_GPR;
        cvtrace->val2 = tswap64(value);
    }
}

void helper_log_value(CPUMIPSState *env, const void* ptr, uint64_t value)
{
    qemu_log_mask(CPU_LOG_INSTR, "%s: " TARGET_FMT_plx "\n", ptr, value);
}

/*
 * Print the instruction to log file.
 */
void helper_log_instruction(CPUMIPSState *env, target_ulong pc)
{
    int isa = (env->hflags & MIPS_HFLAG_M16) == 0 ? 0 : (env->insn_flags & ASE_MICROMIPS) ? 1 : 2;
    if (unlikely(qemu_loglevel_mask(CPU_LOG_INSTR))) {
        CPUState *cs = env_cpu(env);

        /* Disassemble and print instruction. */
        if (isa == 0) {
            log_target_disas(cs, pc, 4);
        } else {
            log_target_disas(cs, pc, 2);
        }
    }

    if (unlikely(qemu_loglevel_mask(CPU_LOG_CVTRACE))) {
        static uint16_t cycles = 0;  /* XXX */
        uint32_t opcode;
        CPUState *cs = env_cpu(env);

        FILE* logfile = qemu_log_lock();
        /* if the logfile is empty we need to emit the cvt magic */
        if (env->cvtrace.version != 0 && ftell(logfile) != 0) {
            /* Write previous instruction trace to log. */
            fwrite(&env->cvtrace, sizeof(env->cvtrace), 1, logfile);
        } else {
            char buffer[sizeof(env->cvtrace)];

            buffer[0] = CVT_QEMU_VERSION;
            g_strlcpy(buffer+1, CVT_QEMU_MAGIC, sizeof(env->cvtrace)-2);
            fwrite(buffer, sizeof(env->cvtrace), 1, logfile);
            cycles = 0;
        }
        qemu_log_unlock(logfile);
        bzero(&env->cvtrace, sizeof(env->cvtrace));
        env->cvtrace.version = CVT_NO_REG;
        env->cvtrace.pc = tswap64(pc);
        env->cvtrace.cycles = tswap16(cycles++);
        env->cvtrace.thread = (uint8_t)cs->cpu_index;
        env->cvtrace.asid = (uint8_t)(env->active_tc.CP0_TCStatus & 0xff);
        env->cvtrace.exception = 31;

        /* Fetch opcode. */
        if (isa == 0) {
            /* mips32/mips64 instruction. */
            opcode = cpu_ldl_code(env, pc);
        } else {
            /* micromips or mips16. */
            opcode = cpu_lduw_code(env, pc);
            if (isa == 1) {
                /* micromips */
                switch (opcode >> 10) {
                case 0x01: case 0x02: case 0x03: case 0x09:
                case 0x0a: case 0x0b:
                case 0x11: case 0x12: case 0x13: case 0x19:
                case 0x1a: case 0x1b:
                case 0x20: case 0x21: case 0x22: case 0x23:
                case 0x28: case 0x29: case 0x2a: case 0x2b:
                case 0x30: case 0x31: case 0x32: case 0x33:
                case 0x38: case 0x39: case 0x3a: case 0x3b:
                    break;
                default:
                    opcode <<= 16;
                    opcode |= cpu_lduw_code(env, pc + 2);
                    break;
                }
            } else {
                /* mips16 */
                switch (opcode >> 11) {
                case 0x03:
                case 0x1e:
                    opcode <<= 16;
                    opcode |= cpu_lduw_code(env, pc + 2);
                    break;
                }
            }
        }
        env->cvtrace.inst = opcode;  /* XXX need bswapped? */
    }
}

#ifndef CONFIG_USER_ONLY

void r4k_dump_tlb(CPUMIPSState *env, int idx)
{
    r4k_tlb_t *tlb = &env->tlb->mmu.r4k.tlb[idx];
    target_ulong pagemask, hi, lo0, lo1;

    if (tlb->EHINV) {
        pagemask = 0;
        hi  = 1 << CP0EnHi_EHINV;
        lo0 = 0;
        lo1 = 0;
    } else {
        pagemask = tlb->PageMask;
        hi = tlb->VPN | tlb->ASID;
        lo0 = tlb->G | (tlb->V0 << 1) | (tlb->D0 << 2) |
#ifdef TARGET_CHERI
            ((target_ulong)tlb->S0 << CP0EnLo_S) |
            ((target_ulong)tlb->L0 << CP0EnLo_L) |
            ((target_ulong)tlb->CLG0 << CP0EnLo_CLG) |
#else
            ((target_ulong)tlb->RI0 << CP0EnLo_RI) |
            ((target_ulong)tlb->XI0 << CP0EnLo_XI) |
#endif
            (tlb->C0 << 3) | (tlb->PFN[0] >> 6);
        lo1 = tlb->G | (tlb->V1 << 1) | (tlb->D1 << 2) |
#ifdef TARGET_CHERI
            ((target_ulong)tlb->S1 << CP0EnLo_S) |
            ((target_ulong)tlb->L1 << CP0EnLo_L) |
            ((target_ulong)tlb->CLG1 << CP0EnLo_CLG) |
#else
            ((target_ulong)tlb->RI1 << CP0EnLo_RI) |
            ((target_ulong)tlb->XI1 << CP0EnLo_XI) |
#endif
            (tlb->C1 << 3) | (tlb->PFN[1] >> 6);
    }
    qemu_log("    Write TLB[%u] = pgmsk:%08lx hi:%08lx lo0:%08lx lo1:%08lx\n",
            idx, pagemask, hi, lo0, lo1);
}


/*
 * Print changed kernel/user/debug mode.
 */
static const char* mips_cpu_get_changed_mode(CPUMIPSState *env)
{
    const char *kernel0, *kernel1, *mode;

#if defined(TARGET_MIPS64)
    if (env->CP0_Status & (1 << CP0St_KX)) {
        kernel0 = "Kernel mode (ERL=0, KX=1)";
        kernel1 = "Kernel mode (ERL=1, KX=1)";
    } else {
        kernel0 = "Kernel mode (ERL=0, KX=0)";
        kernel1 = "Kernel mode (ERL=1, KX=0)";
    }
#else
    kernel0 = "Kernel mode (ERL=0)";
    kernel1 = "Kernel mode (ERL=1)";
#endif

    if (env->CP0_Debug & (1 << CP0DB_DM)) {
        mode = "Debug mode";
    } else if (env->CP0_Status & (1 << CP0St_ERL)) {
        mode = kernel1;
    } else if (env->CP0_Status & (1 << CP0St_EXL)) {
        mode = kernel0;
    } else {
        switch (extract32(env->CP0_Status, CP0St_KSU, 2)) {
        case 0:  mode = kernel0;           break;
        case 1:  mode = "Supervisor mode"; break;
        default: mode = TRACE_MODE_USER;   break;
        }
    }
    return mode;
}

/*
 * Names of coprocessor 0 registers.
 */
static const char *cop0_name[32*8] = {
/*0*/   "Index",        "MVPControl",   "MVPConf0",     "MVPConf1",
        0,              0,              0,              0,
/*1*/   "Random",       "VPEControl",   "VPEConf0",     "VPEConf1",
        "YQMask",       "VPESchedule",  "VPEScheFBack", "VPEOpt",
/*2*/   "EntryLo0",     "TCStatus",     "TCBind",       "TCRestart",
        "TCHalt",       "TCContext",    "TCSchedule",   "TCScheFBack",
/*3*/   "EntryLo1",     0,              0,              0,
        0,              0,              0,              "TCOpt",
/*4*/   "Context",      "ContextConfig","UserLocal",    "XContextConfig",
        0,              0,              0,              0,
/*5*/   "PageMask",     "PageGrain",    "SegCtl0",      "SegCtl1",
        "SegCtl2",      0,              0,              0,
/*6*/   "Wired",        "SRSConf0",     "SRSConf1",     "SRSConf2",
        "SRSConf3",     "SRSConf4",     0,              0,
/*7*/   "HWREna",       0,              0,              0,
        0,              0,              0,              0,
/*8*/   "BadVAddr",     "BadInstr",     "BadInstrP",    0,
        0,              0,              0,              0,
/*9*/   "Count",        0,              0,              0,
        0,              0,              0,              0,
/*10*/  "EntryHi",      0,              0,              0,
        0,              "MSAAccess",    "MSASave",      "MSARequest",
/*11*/  "Compare",      0,              0,              0,
        0,              0,              0,              0,
/*12*/  "Status",       "IntCtl",       "SRSCtl",       "SRSMap",
        "ViewIPL",      "SRSMap2",      0,              0,
/*13*/  "Cause",        0,              0,              0,
        "ViewRIPL",     "NestedExc",    0,              0,
/*14*/  "EPC",          0,              "NestedEPC",    0,
        0,              0,              0,              0,
/*15*/  "PRId",         "EBase",        "CDMMBase",     "CMGCRBase",
        0,              0,              0,              0,
/*16*/  "Config",       "Config1",      "Config2",      "Config3",
        "Config4",      "Config5",      "Config6",      "Config7",
/*17*/  "LLAddr",       "internal_lladdr (virtual)", "internal_llval", "internal_linkedflag",
        0,              0,              0,              0,
/*18*/  "WatchLo",      "WatchLo1",     "WatchLo2",     "WatchLo3",
        "WatchLo4",     "WatchLo5",     "WatchLo6",     "WatchLo7",
/*19*/  "WatchHi",      "WatchHi1",     "WatchHi2",     "WatchHi3",
        "WatchHi4",     "WatchHi5",     "WatchHi6",     "WatchHi7",
/*20*/  "XContext",     0,              0,              0,
        0,              0,              0,              0,
/*21*/  0,              0,              0,              0,
        0,              0,              0,              0,
/*22*/  0,              0,              0,              0,
        0,              0,              0,              0,
/*23*/  "Debug",        "TraceControl", "TraceControl2","UserTraceData",
        "TraceIBPC",    "TraceDBPC",    "Debug2",       0,
/*24*/  "DEPC",         0,              "TraceControl3","UserTraceData2",
        0,              0,              0,              0,
/*25*/  "PerfCnt",      "PerfCnt1",     "PerfCnt2",     "PerfCnt3",
        "PerfCnt4",     "PerfCnt5",     "PerfCnt6",     "PerfCnt7",
/*26*/  "ErrCtl",       0,              0,              0,
        0,              0,              0,              0,
/*27*/  "CacheErr",     0,              0,              0,
        0,              0,              0,              0,
/*28*/  "ITagLo",       "IDataLo",      "DTagLo",       "DDataLo",
        "L23TagLo",     "L23DataLo",    0,              0,
/*29*/  "ITagHi",       "IDataHi",      "DTagHi",       0,
        0,              "L23DataHi",    0,              0,
/*30*/  "ErrorEPC",     0,              0,              0,
        0,              0,              0,              0,
/*31*/  "DESAVE",       0,              "KScratch1",    "KScratch2",
        "KScratch3",    "KScratch4",    "KScratch5",    "KScratch6",
};

/*
 * Print changed values of COP0 registers.
 */
static void dump_changed_cop0_reg(CPUMIPSState *env, int idx,
        target_ulong value)
{
    if (value != env->last_cop0[idx]) {
        env->last_cop0[idx] = value;
        if (cop0_name[idx])
            qemu_log("    Write %s = " TARGET_FMT_lx "\n",
                    cop0_name[idx], value);
        else
            qemu_log("    Write (idx=%d) = " TARGET_FMT_lx "\n",
                    idx, value);

    }
}

/*
 * Print changed values of COP0 registers.
 */
static void dump_changed_cop0(CPUMIPSState *env)
{
    dump_changed_cop0_reg(env, 0*8 + 0, env->CP0_Index);
    if (env->CP0_Config3 & (1 << CP0C3_MT)) {
        dump_changed_cop0_reg(env, 0*8 + 1, env->mvp->CP0_MVPControl);
        dump_changed_cop0_reg(env, 0*8 + 2, env->mvp->CP0_MVPConf0);
        dump_changed_cop0_reg(env, 0*8 + 3, env->mvp->CP0_MVPConf1);

        dump_changed_cop0_reg(env, 1*8 + 1, env->CP0_VPEControl);
        dump_changed_cop0_reg(env, 1*8 + 2, env->CP0_VPEConf0);
        dump_changed_cop0_reg(env, 1*8 + 3, env->CP0_VPEConf1);
        dump_changed_cop0_reg(env, 1*8 + 4, env->CP0_YQMask);
        dump_changed_cop0_reg(env, 1*8 + 5, env->CP0_VPESchedule);
        dump_changed_cop0_reg(env, 1*8 + 6, env->CP0_VPEScheFBack);
        dump_changed_cop0_reg(env, 1*8 + 7, env->CP0_VPEOpt);
    }

    dump_changed_cop0_reg(env, 2*8 + 0, env->CP0_EntryLo0);
    if (env->CP0_Config3 & (1 << CP0C3_MT)) {
        dump_changed_cop0_reg(env, 2*8 + 1, env->active_tc.CP0_TCStatus);
        dump_changed_cop0_reg(env, 2*8 + 2, env->active_tc.CP0_TCBind);
        dump_changed_cop0_reg(env, 2*8 + 3, env->active_tc.PC);
        dump_changed_cop0_reg(env, 2*8 + 4, env->active_tc.CP0_TCHalt);
        dump_changed_cop0_reg(env, 2*8 + 5, env->active_tc.CP0_TCContext);
        dump_changed_cop0_reg(env, 2*8 + 6, env->active_tc.CP0_TCSchedule);
        dump_changed_cop0_reg(env, 2*8 + 7, env->active_tc.CP0_TCScheFBack);
    }

    dump_changed_cop0_reg(env, 3*8 + 0, env->CP0_EntryLo1);

    dump_changed_cop0_reg(env, 4*8 + 0, env->CP0_Context);
    /* 4/1 not implemented - ContextConfig */
    dump_changed_cop0_reg(env, 4*8 + 2, env->active_tc.CP0_UserLocal);
    /* 4/3 not implemented - XContextConfig */

    dump_changed_cop0_reg(env, 5*8 + 0, env->CP0_PageMask);
    dump_changed_cop0_reg(env, 5*8 + 1, env->CP0_PageGrain);

    dump_changed_cop0_reg(env, 6*8 + 0, env->CP0_Wired);
    dump_changed_cop0_reg(env, 6*8 + 1, env->CP0_SRSConf0);
    dump_changed_cop0_reg(env, 6*8 + 2, env->CP0_SRSConf1);
    dump_changed_cop0_reg(env, 6*8 + 3, env->CP0_SRSConf2);
    dump_changed_cop0_reg(env, 6*8 + 4, env->CP0_SRSConf3);
    dump_changed_cop0_reg(env, 6*8 + 5, env->CP0_SRSConf4);

    dump_changed_cop0_reg(env, 7*8 + 0, env->CP0_HWREna);

    dump_changed_cop0_reg(env, 8*8 + 0, env->CP0_BadVAddr);
    if (env->CP0_Config3 & (1 << CP0C3_BI))
        dump_changed_cop0_reg(env, 8*8 + 1, env->CP0_BadInstr);
    if (env->CP0_Config3 & (1 << CP0C3_BP))
        dump_changed_cop0_reg(env, 8*8 + 2, env->CP0_BadInstrP);

    dump_changed_cop0_reg(env, 10*8 + 0, env->CP0_EntryHi);

    dump_changed_cop0_reg(env, 11*8 + 0, env->CP0_Compare);

    dump_changed_cop0_reg(env, 12*8 + 0, env->CP0_Status);
    dump_changed_cop0_reg(env, 12*8 + 1, env->CP0_IntCtl);
    dump_changed_cop0_reg(env, 12*8 + 2, env->CP0_SRSCtl);
    dump_changed_cop0_reg(env, 12*8 + 3, env->CP0_SRSMap);

    dump_changed_cop0_reg(env, 13*8 + 0, env->CP0_Cause);

    dump_changed_cop0_reg(env, 14*8 + 0, get_CP0_EPC(env));

    dump_changed_cop0_reg(env, 15*8 + 0, env->CP0_PRid);
    dump_changed_cop0_reg(env, 15*8 + 1, env->CP0_EBase);

    dump_changed_cop0_reg(env, 16*8 + 0, env->CP0_Config0);
    dump_changed_cop0_reg(env, 16*8 + 1, env->CP0_Config1);
    dump_changed_cop0_reg(env, 16*8 + 2, env->CP0_Config2);
    dump_changed_cop0_reg(env, 16*8 + 3, env->CP0_Config3);
    dump_changed_cop0_reg(env, 16*8 + 4, env->CP0_Config4);
    dump_changed_cop0_reg(env, 16*8 + 5, env->CP0_Config5);
    dump_changed_cop0_reg(env, 16*8 + 6, env->CP0_Config6);
    dump_changed_cop0_reg(env, 16*8 + 7, env->CP0_Config7);

    dump_changed_cop0_reg(env, 17*8 + 0, env->CP0_LLAddr >> env->CP0_LLAddr_shift);
    dump_changed_cop0_reg(env, 17*8 + 1, env->lladdr);
    dump_changed_cop0_reg(env, 17*8 + 2, env->llval);
#ifdef TARGET_CHERI
    dump_changed_cop0_reg(env, 17*8 + 3, env->linkedflag);
#endif

    dump_changed_cop0_reg(env, 18*8 + 0, env->CP0_WatchLo[0]);
    dump_changed_cop0_reg(env, 18*8 + 1, env->CP0_WatchLo[1]);
    dump_changed_cop0_reg(env, 18*8 + 2, env->CP0_WatchLo[2]);
    dump_changed_cop0_reg(env, 18*8 + 3, env->CP0_WatchLo[3]);
    dump_changed_cop0_reg(env, 18*8 + 4, env->CP0_WatchLo[4]);
    dump_changed_cop0_reg(env, 18*8 + 5, env->CP0_WatchLo[5]);
    dump_changed_cop0_reg(env, 18*8 + 6, env->CP0_WatchLo[6]);
    dump_changed_cop0_reg(env, 18*8 + 7, env->CP0_WatchLo[7]);

    dump_changed_cop0_reg(env, 19*8 + 0, env->CP0_WatchHi[0]);
    dump_changed_cop0_reg(env, 19*8 + 1, env->CP0_WatchHi[1]);
    dump_changed_cop0_reg(env, 19*8 + 2, env->CP0_WatchHi[2]);
    dump_changed_cop0_reg(env, 19*8 + 3, env->CP0_WatchHi[3]);
    dump_changed_cop0_reg(env, 19*8 + 4, env->CP0_WatchHi[4]);
    dump_changed_cop0_reg(env, 19*8 + 5, env->CP0_WatchHi[5]);
    dump_changed_cop0_reg(env, 19*8 + 6, env->CP0_WatchHi[6]);
    dump_changed_cop0_reg(env, 19*8 + 7, env->CP0_WatchHi[7]);

#if defined(TARGET_MIPS64)
    dump_changed_cop0_reg(env, 20*8 + 0, env->CP0_XContext);
#endif

    dump_changed_cop0_reg(env, 21*8 + 0, env->CP0_Framemask);

    /* 22/x not defined */

    dump_changed_cop0_reg(env, 23*8 + 0, helper_mfc0_debug(env));

    dump_changed_cop0_reg(env, 24*8 + 0, env->CP0_DEPC);

    dump_changed_cop0_reg(env, 25*8 + 0, env->CP0_Performance0);

    /* 26/0 - ErrCtl */
    dump_changed_cop0_reg(env, 25*8 + 0, env->CP0_ErrCtl);

    /* 27/0 not implemented - CacheErr */

    dump_changed_cop0_reg(env, 28*8 + 0, env->CP0_TagLo);
    dump_changed_cop0_reg(env, 28*8 + 1, env->CP0_DataLo);

    dump_changed_cop0_reg(env, 29*8 + 0, env->CP0_TagHi);
    dump_changed_cop0_reg(env, 29*8 + 1, env->CP0_DataHi);

    dump_changed_cop0_reg(env, 30*8 + 0, get_CP0_ErrorEPC(env));

    dump_changed_cop0_reg(env, 31*8 + 0, env->CP0_DESAVE);
    dump_changed_cop0_reg(env, 31*8 + 2, env->CP0_KScratch[0]);
    dump_changed_cop0_reg(env, 31*8 + 3, env->CP0_KScratch[1]);
    dump_changed_cop0_reg(env, 31*8 + 4, env->CP0_KScratch[2]);
    dump_changed_cop0_reg(env, 31*8 + 5, env->CP0_KScratch[3]);
    dump_changed_cop0_reg(env, 31*8 + 6, env->CP0_KScratch[4]);
    dump_changed_cop0_reg(env, 31*8 + 7, env->CP0_KScratch[5]);
}
#endif /* !CONFIG_USER_ONLY */


/*
 * Print changed values of GPR, HI/LO and DSPControl registers.
 */
static void dump_changed_regs(CPUMIPSState *env)
{
    TCState *cur = &env->active_tc;

#ifndef TARGET_MIPS64
    static const char * const gpr_name[32] =
    {
      "zero", "at",   "v0",   "v1",   "a0",   "a1",   "a2",   "a3",
      "t0",   "t1",   "t2",   "t3",   "t4",   "t5",   "t6",   "t7",
      "s0",   "s1",   "s2",   "s3",   "s4",   "s5",   "s6",   "s7",
      "t8",   "t9",   "k0",   "k1",   "gp",   "sp",   "s8",   "ra"
    };
#else
    // Use n64 register names
    static const char * const gpr_name[32] =
    {
      "zero", "at",   "v0",   "v1",   "a0",   "a1",   "a2",   "a3",
      "a4",   "a5",   "a6",   "a7",   "t0",   "t1",   "t2",   "t3",
      "s0",   "s1",   "s2",   "s3",   "s4",   "s5",   "s6",   "s7",
      "t8",   "t9",   "k0",   "k1",   "gp",   "sp",   "s8",   "ra"
    };
#endif

    int i;

    for (i=1; i<32; i++) {
        if (cur->gpr[i] != env->last_gpr[i]) {
            env->last_gpr[i] = cur->gpr[i];
            cvtrace_dump_gpr(&env->cvtrace, cur->gpr[i]);
            qemu_log_mask(CPU_LOG_INSTR, "    Write %s = " TARGET_FMT_lx "\n",
                          gpr_name[i], cur->gpr[i]);
        }
    }
#ifdef TARGET_CHERI
    dump_changed_cop2(env, cur);
#endif
}


static void update_tracing_on_mode_change(CPUMIPSState *env, const char* new_mode)
{
    if (!env->user_only_tracing_enabled) {
            // Handle cases where QEMU was started with -d user-instr
        if (qemu_loglevel_mask(CPU_LOG_USER_ONLY)) {
            env->user_only_tracing_enabled = true;
            env->tracing_suspended = true;
        } else {
            return;
        }
    }
    if (IN_USERSPACE(env)) {
        assert(strcmp(new_mode, TRACE_MODE_USER) != 0);
        /* When changing from user mode to kernel mode disable tracing */
        user_trace_dbg("%s -> %s: 0x%lx ASID %lu -- switching off tracing \n",
            env->last_mode, new_mode, env->active_tc.PC, env->CP0_EntryHi & 0xFF);
        env->tracing_suspended = true;
        qemu_set_log(qemu_loglevel & ~cl_default_trace_format);
    } else if (strcmp(new_mode, TRACE_MODE_USER) == 0) {
        /* When changing back to user mode restore instruction tracing */
        assert(!IN_USERSPACE(env));
        if (env->trace_explicitly_disabled) {
            user_trace_dbg("Not turning on tracing on switch %s -> %s 0x%lx. "
                "Tracing was explicitly disabled, ASID=%lu\n",
                env->last_mode, new_mode, env->active_tc.PC, env->CP0_EntryHi & 0xFF);
        } else if (env->tracing_suspended) {
            qemu_set_log(qemu_loglevel | cl_default_trace_format);
            user_trace_dbg("%s -> %s 0x%lx ASID %lu -- switching on tracing\n",
                env->last_mode, new_mode, env->active_tc.PC, env->CP0_EntryHi & 0xFF);
            env->tracing_suspended = false;
        }
    }
}

/*
 * Print the changed processor state.
 */
void helper_dump_changed_state(CPUMIPSState *env)
{
    const char* new_mode = mips_cpu_get_changed_mode(env);
    /* Testing pointer equality is fine, it always points to the same constants */
    if (new_mode != env->last_mode) {
        update_tracing_on_mode_change(env, new_mode);
        env->last_mode = new_mode;
        qemu_log_mask(CPU_LOG_INSTR, "--- %s\n", new_mode);
    }

    if (qemu_loglevel_mask(CPU_LOG_INSTR | CPU_LOG_CVTRACE)) {
        /* Print changed state: GPR, Cap. */
        dump_changed_regs(env);
    }

    if (qemu_loglevel_mask(CPU_LOG_INSTR)) {
        /* Print change state: HI/LO COP0 (not included in CVTRACE) */
        dump_changed_cop0(env);
    }
}

/*
 * dump non-capability data to cvtrace entry
 */
static inline void cvtrace_dump_gpr_ldst(cvtrace_t *cvtrace, uint8_t version,
        uint64_t addr, uint64_t value)
{
    if (qemu_loglevel_mask(CPU_LOG_CVTRACE)) {
        cvtrace->version = version;
        cvtrace->val1 = tswap64(addr);
        cvtrace->val2 = tswap64(value);
    }
}
#define cvtrace_dump_gpr_load(trace, addr, val)          \
    cvtrace_dump_gpr_ldst(trace, CVT_LD_GPR, addr, val)
#define cvtrace_dump_gpr_store(trace, addr, val)         \
    cvtrace_dump_gpr_ldst(trace, CVT_ST_GPR, addr, val)

/*
 * Print the memory store to log file.
 */
void helper_dump_store(CPUMIPSState *env, target_ulong addr,target_ulong value, MemOp op)
{

    if (likely(!(qemu_loglevel_mask(CPU_LOG_INSTR) |
                 qemu_loglevel_mask(CPU_LOG_CVTRACE))))
        return;
    if (qemu_loglevel_mask(CPU_LOG_CVTRACE)) {
        cvtrace_dump_gpr_store(&env->cvtrace, addr, value);
        return;
    }

    // FIXME: value printed is not correct for sdl!
    // FIXME: value printed is not correct for sdr!
    // FIXME: value printed is not correct for swl!
    // FIXME: value printed is not correct for swr!
    switch (memop_size(op)) {
    case 8:
        qemu_log("    Memory Write [" TARGET_FMT_lx "] = " TARGET_FMT_lx "\n",
                 addr, value);
        break;
    case 4:
        qemu_log("    Memory Write [" TARGET_FMT_lx "] = %08x\n", addr,
                 (uint32_t)value);
        break;
    case 2:
        qemu_log("    Memory Write [" TARGET_FMT_lx "] = %04x\n", addr,
                 (uint16_t)value);
        break;
    case 1:
        qemu_log("    Memory Write [" TARGET_FMT_lx "] = %02x\n", addr,
                 (uint8_t)value);
        break;
    default:
        tcg_abort();
    }
}

void helper_dump_store32(CPUMIPSState *env, target_ulong addr, uint32_t value, MemOp op)
{
    helper_dump_store(env, addr, (target_ulong)value, op);
}

/*
 * Print the memory load to log file.
 */
void helper_dump_load(CPUMIPSState *env, target_ulong addr, target_ulong value, MemOp op)
{
    if (likely(!(qemu_loglevel_mask(CPU_LOG_INSTR) |
                 qemu_loglevel_mask(CPU_LOG_CVTRACE))))
        return;
    if (qemu_loglevel_mask(CPU_LOG_CVTRACE)) {
        cvtrace_dump_gpr_load(&env->cvtrace, addr, value);
        return;
    }
    // FIXME: cloadtags not correct
    switch (memop_size(op)) {
    case 8:
        qemu_log("    Memory Read [" TARGET_FMT_lx "] = " TARGET_FMT_lx "\n",
                 addr, value);
        break;
    case 4:
        qemu_log("    Memory Read [" TARGET_FMT_lx "] = %08x\n", addr,
                 (uint32_t)value);
        break;
    case 2:
        qemu_log("    Memory Read [" TARGET_FMT_lx "] = %04x\n", addr,
                 (uint16_t)value);
        break;
    case 1:

        qemu_log("    Memory Read [" TARGET_FMT_lx "] = %02x\n", addr,
                 (uint8_t)value);
        break;
    default:
        tcg_abort();
    }
}

void helper_dump_load32(CPUMIPSState *env, target_ulong addr, uint32_t value, MemOp op)
{
    helper_dump_load(env, addr, (target_ulong)value, op);
}

#endif // CONFIG_MIPS_LOG_INSTR

static void simple_dump_state(CPUMIPSState *env, FILE *f,
                              fprintf_function cpu_fprintf)
{

/* gxemul compat:
    cpu_fprintf(f, "pc = 0x" TARGET_FMT_lx "\n", env->active_tc.PC);
    cpu_fprintf(f, "hi = 0x" TARGET_FMT_lx "    lo = 0x" TARGET_FMT_lx "\n",
            env->active_tc.HI[0], env->active_tc.LO[0]);
    cpu_fprintf(f, "                       ""    s0 = 0x" TARGET_FMT_lx "\n",
            env->active_tc.gpr[16]);
    cpu_fprintf(f, "at = 0x" TARGET_FMT_lx "    s1 = 0x" TARGET_FMT_lx "\n",
            env->active_tc.gpr[1], env->active_tc.gpr[17]);
    cpu_fprintf(f, "v0 = 0x" TARGET_FMT_lx "    s2 = 0x" TARGET_FMT_lx "\n",
            env->active_tc.gpr[2], env->active_tc.gpr[18]);
    cpu_fprintf(f, "v1 = 0x" TARGET_FMT_lx "    s3 = 0x" TARGET_FMT_lx "\n",
            env->active_tc.gpr[3], env->active_tc.gpr[19]);
    cpu_fprintf(f, "a0 = 0x" TARGET_FMT_lx "    s4 = 0x" TARGET_FMT_lx "\n",
            env->active_tc.gpr[4], env->active_tc.gpr[20]);
    cpu_fprintf(f, "a1 = 0x" TARGET_FMT_lx "    s5 = 0x" TARGET_FMT_lx "\n",
            env->active_tc.gpr[5], env->active_tc.gpr[21]);
    cpu_fprintf(f, "a2 = 0x" TARGET_FMT_lx "    s6 = 0x" TARGET_FMT_lx "\n",
            env->active_tc.gpr[6], env->active_tc.gpr[22]);
    cpu_fprintf(f, "a3 = 0x" TARGET_FMT_lx "    s7 = 0x" TARGET_FMT_lx "\n",
            env->active_tc.gpr[7], env->active_tc.gpr[23]);
    cpu_fprintf(f, "t0 = 0x" TARGET_FMT_lx "    t8 = 0x" TARGET_FMT_lx "\n",
            env->active_tc.gpr[8], env->active_tc.gpr[24]);
    cpu_fprintf(f, "t1 = 0x" TARGET_FMT_lx "    t9 = 0x" TARGET_FMT_lx "\n",
            env->active_tc.gpr[9], env->active_tc.gpr[25]);
    cpu_fprintf(f, "t2 = 0x" TARGET_FMT_lx "    k0 = 0x" TARGET_FMT_lx "\n",
            env->active_tc.gpr[10], env->active_tc.gpr[26]);
    cpu_fprintf(f, "t3 = 0x" TARGET_FMT_lx "    k1 = 0x" TARGET_FMT_lx "\n",
            env->active_tc.gpr[11], env->active_tc.gpr[27]);
    cpu_fprintf(f, "t4 = 0x" TARGET_FMT_lx "    gp = 0x" TARGET_FMT_lx "\n",
            env->active_tc.gpr[12], env->active_tc.gpr[28]);
    cpu_fprintf(f, "t5 = 0x" TARGET_FMT_lx "    sp = 0x" TARGET_FMT_lx "\n",
            env->active_tc.gpr[13], env->active_tc.gpr[29]);
    cpu_fprintf(f, "t6 = 0x" TARGET_FMT_lx "    fp = 0x" TARGET_FMT_lx "\n",
            env->active_tc.gpr[14], env->active_tc.gpr[30]);
    cpu_fprintf(f, "t7 = 0x" TARGET_FMT_lx "    ra = 0x" TARGET_FMT_lx "\n",
            env->active_tc.gpr[15], env->active_tc.gpr[31]);
*/

    /* sim compatible register dump: */
    cpu_fprintf(f, "DEBUG MIPS COREID 0\n");
    cpu_fprintf(f, "DEBUG MIPS PC 0x" TARGET_FMT_lx "\n", env->active_tc.PC);
    cpu_fprintf(f, "DEBUG MIPS REG 00 0x" TARGET_FMT_lx "\n", env->active_tc.gpr[0]);
    cpu_fprintf(f, "DEBUG MIPS REG 01 0x" TARGET_FMT_lx "\n", env->active_tc.gpr[1]);
    cpu_fprintf(f, "DEBUG MIPS REG 02 0x" TARGET_FMT_lx "\n", env->active_tc.gpr[2]);
    cpu_fprintf(f, "DEBUG MIPS REG 03 0x" TARGET_FMT_lx "\n", env->active_tc.gpr[3]);
    cpu_fprintf(f, "DEBUG MIPS REG 04 0x" TARGET_FMT_lx "\n", env->active_tc.gpr[4]);
    cpu_fprintf(f, "DEBUG MIPS REG 05 0x" TARGET_FMT_lx "\n", env->active_tc.gpr[5]);
    cpu_fprintf(f, "DEBUG MIPS REG 06 0x" TARGET_FMT_lx "\n", env->active_tc.gpr[6]);
    cpu_fprintf(f, "DEBUG MIPS REG 07 0x" TARGET_FMT_lx "\n", env->active_tc.gpr[7]);
    cpu_fprintf(f, "DEBUG MIPS REG 08 0x" TARGET_FMT_lx "\n", env->active_tc.gpr[8]);
    cpu_fprintf(f, "DEBUG MIPS REG 09 0x" TARGET_FMT_lx "\n", env->active_tc.gpr[9]);
    cpu_fprintf(f, "DEBUG MIPS REG 10 0x" TARGET_FMT_lx "\n", env->active_tc.gpr[10]);
    cpu_fprintf(f, "DEBUG MIPS REG 11 0x" TARGET_FMT_lx "\n", env->active_tc.gpr[11]);
    cpu_fprintf(f, "DEBUG MIPS REG 12 0x" TARGET_FMT_lx "\n", env->active_tc.gpr[12]);
    cpu_fprintf(f, "DEBUG MIPS REG 13 0x" TARGET_FMT_lx "\n", env->active_tc.gpr[13]);
    cpu_fprintf(f, "DEBUG MIPS REG 14 0x" TARGET_FMT_lx "\n", env->active_tc.gpr[14]);
    cpu_fprintf(f, "DEBUG MIPS REG 15 0x" TARGET_FMT_lx "\n", env->active_tc.gpr[15]);
    cpu_fprintf(f, "DEBUG MIPS REG 16 0x" TARGET_FMT_lx "\n", env->active_tc.gpr[16]);
    cpu_fprintf(f, "DEBUG MIPS REG 17 0x" TARGET_FMT_lx "\n", env->active_tc.gpr[17]);
    cpu_fprintf(f, "DEBUG MIPS REG 18 0x" TARGET_FMT_lx "\n", env->active_tc.gpr[18]);
    cpu_fprintf(f, "DEBUG MIPS REG 19 0x" TARGET_FMT_lx "\n", env->active_tc.gpr[19]);
    cpu_fprintf(f, "DEBUG MIPS REG 20 0x" TARGET_FMT_lx "\n", env->active_tc.gpr[20]);
    cpu_fprintf(f, "DEBUG MIPS REG 21 0x" TARGET_FMT_lx "\n", env->active_tc.gpr[21]);
    cpu_fprintf(f, "DEBUG MIPS REG 22 0x" TARGET_FMT_lx "\n", env->active_tc.gpr[22]);
    cpu_fprintf(f, "DEBUG MIPS REG 23 0x" TARGET_FMT_lx "\n", env->active_tc.gpr[23]);
    cpu_fprintf(f, "DEBUG MIPS REG 24 0x" TARGET_FMT_lx "\n", env->active_tc.gpr[24]);
    cpu_fprintf(f, "DEBUG MIPS REG 25 0x" TARGET_FMT_lx "\n", env->active_tc.gpr[25]);
    cpu_fprintf(f, "DEBUG MIPS REG 26 0x" TARGET_FMT_lx "\n", env->active_tc.gpr[26]);
    cpu_fprintf(f, "DEBUG MIPS REG 27 0x" TARGET_FMT_lx "\n", env->active_tc.gpr[27]);
    cpu_fprintf(f, "DEBUG MIPS REG 28 0x" TARGET_FMT_lx "\n", env->active_tc.gpr[28]);
    cpu_fprintf(f, "DEBUG MIPS REG 29 0x" TARGET_FMT_lx "\n", env->active_tc.gpr[29]);
    cpu_fprintf(f, "DEBUG MIPS REG 30 0x" TARGET_FMT_lx "\n", env->active_tc.gpr[30]);
    cpu_fprintf(f, "DEBUG MIPS REG 31 0x" TARGET_FMT_lx "\n", env->active_tc.gpr[31]);

}

void helper_mtc0_dumpstate(CPUMIPSState *env, target_ulong arg1)
{
    FILE* logfile = qemu_log_enabled() ? qemu_log_lock() : stderr;
#if 0
    cpu_dump_state(env_cpu(env), logfile, fprintf, CPU_DUMP_CODE);
#else
    simple_dump_state(env, logfile, fprintf);
#endif
    if (logfile != stderr)
        qemu_log_unlock(logfile);
}
