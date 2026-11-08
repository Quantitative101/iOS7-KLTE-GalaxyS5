# Galaxy S5: true multi-core (SMP) in XNU — scoping

Written 2026-11-08 after runs 129–131. No SMP code exists yet; this is the plan and what is known.

## Why

A home-screen swipe frame is ~50 ms at 1.5–1.7 GHz:

| Part | Time |
|---|---|
| CoreAnimation and SpringBoard CPU work | ~40–45 ms |
| backboardd drawing (software GL) | 2–9 ms |
| Kernel present and panel mirror | ~7 ms (moved to CPU1 in run 131) |

Helper cores outside XNU can only take the bottom two rows. The top row is ordinary iOS threads; only the XNU
scheduler can spread it, and the other per-app work, over CPU1–3.

## What exists today

- **Hardware bring-up works.** Run 129 woke CPU1:
  - SCM legacy atomic `set_boot_addr` (smc #0, r0 0x401222, cold flags CPU1 0x01, CPU2 0x08, CPU3 0x20);
  - kpssv2 power-up through ACC 0xF9098000 (CPU1), 0xF90A8000 (CPU2) and 0xF90B8000 (CPU3), plus L2 SAW 0xF9012000.

  Run 131 ran CPU1 with the MMU and caches on, using the kernel's own translation table (`s5_helper.c` trampoline).
- **XNU is uniprocessor by construction:**
  - `osfmk/arm/cpu.c`: `get_cpu_number()` returns 0, `cpu_start()` does nothing for other CPUs, there is a single
    `cpu_data_master`, and `current_cpu_datap()` indexes `cpu_datap(smp_number)`.
  - `osfmk/arm/mp.c`: `real_ncpus = 1`; `cause_ast_check` / `init_ast_check` are empty, so no IPI to preempt
    another CPU.
  - `osfmk/arm/machine_routines.c` `ml_processor_register`: "No secondary CPU ... no IPI implementation",
    `ipi_handler = NULL`.
  - `osfmk/arm/pmap.c` `pmap_flush_tlbs`: "xxx broadcast IPI to all other CPUs to flush". TLB maintenance is local
    only.
- **The locks are real:** `osfmk/arm/hw_lock.s` and `atomic.s` use `ldrex`/`strex` (`NO_EXCLUSIVES` is not defined).
  They still need an audit for `dmb` barriers and for spinning with interrupts or preemption disabled.
- **current_thread** lives in TPIDRPRW (c13,c0,4) and is set in `cswitch.s`. That register is per-CPU on ARMv7, which
  is good.
- **Memory attributes:** all RAM is mapped write-through and non-shareable (`pmap_get_cache_attributes`,
  `l2_map_linear_range`). Krait coherency between cores needs shareable (S) mappings, and ideally write-back
  (WB-WA), the way Linux maps memory on SMP.
- **Interrupts and timer:** QGIC2 at 0xF9000000/0xF9002000, set up for CPU0 only (`pexpert/arm/j1_timer.c`). The ARM
  generic timer (CNTP, PPI) is per-CPU, so each CPU needs its own GIC CPU-interface init and timer programming.
- **Clock:** CPU0 runs at 1728 MHz (L=90). CPU1–3 each have their own HFPLL at 0xF909A000, 0xF90AA000 and 0xF90BA000
  (unverified; CPU0's is 0xF908A000) and mux registers 0x5501, 0x6501 and 0x7501. They boot at roughly 0.9 GHz.

## Work items, in dependency order

1. **Coherent memory (prerequisite for everything).**
   - Mark normal RAM shareable (S bit in small-page and section descriptors, TTBR S/NOS bits).
   - Move to write-back write-allocate, with the cache maintenance that implies: PTE writes cleaned or walked
     coherently; exec pages D-clean plus I-invalidate (already present); attribute changes and page reuse clean and
     invalidate.
   - Set ACTLR.SMP-equivalent if Krait needs it (Linux leaves ACTLR alone on Krait).
   - **Risk: high.** This touches every mapping. A single-core build can test it first (all four steps below depend
     on it).
2. **Per-CPU identity and data.**
   - `get_cpu_number()` from MPIDR[1:0].
   - `cpu_data_ptr[]` for 4 CPUs, each with its own interrupt stack, exception stacks and FIQ/abort stacks.
   - `current_cpu_datap()` per CPU (TPIDRURO/TPIDRPRW or MPIDR lookup).
   - `master_cpu`, `real_ncpus = 4`, `machine_info.max_cpus/avail_cpus`, `processor_avail_count`.
3. **Secondary start path.**
   - Reuse the run 131 trampoline: CPU wakes, MMU on with the kernel tables, then a kernel `secondary_start()` instead
     of the helper loop.
   - It sets up its stacks, VBAR (the kernel's exception vectors), GIC CPU interface and per-CPU timer, then calls
     `processor_start` / `slave_main` so the scheduler gets a new processor (`ml_processor_register` must create a
     real `processor_t`).
   - `cpu_start(cpu)` performs the SCM / ACC wake for that CPU.
4. **Inter-processor interrupts.**
   - GIC SGIs (GICD_SGIR 0xF9000F00) for AST/preempt (`cause_ast_check`), TLB shootdown (`pmap_flush_tlbs` broadcast
     plus wait), `cpu_signal` / xcall, and halt and debugger stop.
   - Each CPU's SGI handler is dispatched from the IRQ path.
5. **Timers per CPU.** The etimer/rtclock deadline is currently global (`clock_decrementer`). Each CPU programs its own
   CNTP_CVAL and takes its own PPI. `mach_absolute_time` already comes from the shared 19.2 MHz counter, so it is
   coherent.
6. **Kernel audit for UP assumptions:**
   - Places that disable interrupts as a lock (spl, `ml_set_interrupts_enabled` used for mutual exclusion);
   - the J1/Leo port's own drivers (IOS7Leo*, S5 touch/RPM/SPMI/SDHCI, the log ring and console);
   - pmap globals;
   - the `cpu_data` single instance;
   - `machine_idle` (WFI per CPU).
7. **Power and thermal.**
   - With four cores busy, power and heat rise; the voltage question (each core at LK's voltage) matters more.
   - Start with CPU1 only, then CPU2 and CPU3.

## Suggested staging

| Stage | What | How to test |
|---|---|---|
| A | Shareable + write-back memory, still on one CPU | Normal use is stable; no stale-data symptoms |
| B | Per-CPU data and `cpu_number` from MPIDR, still one CPU | Boots as before |
| C | CPU1 joins as an idle `processor_t` (scheduler does not use it yet); IPIs and TLB shootdown live | CPU1 receives SGIs; stays in idle WFI |
| D | Let the scheduler run threads on CPU1 | Watch for lock and driver races |
| E | CPU2 and CPU3 | Scale-out |

Each stage is several device runs. Stage A is the riskiest and the most valuable on its own (write-back memory may
also speed up the single-core system).

## Estimate

Weeks of iterative work. Expect frequent hangs and panics during stages A, C and D. All are recoverable: the watchdog
returns to TWRP, and the last good boot image is reflashed.
