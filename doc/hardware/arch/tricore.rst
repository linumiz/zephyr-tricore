.. SPDX-FileCopyrightText: Copyright (c) 2026 Infineon Technologies AG,
.. SPDX-FileCopyrightText: or an affiliate of Infineon Technologies AG. All rights reserved.
.. SPDX-FileCopyrightText: Copyright (c) 2026 Linumiz
..
.. SPDX-License-Identifier: Apache-2.0

.. _tricore_developer_guide:

TriCore AURIX Developer Guide
#############################

Overview
********

This page describes the status of the Infineon TriCore AURIX architecture port
in Zephyr and the aspects that matter when developing Zephyr applications for
AURIX platforms.

The `TriCore <https://www.infineon.com/cms/en/product/microcontroller/32-bit-tricore-microcontroller/>`_
architecture is a unified 32-bit microcontroller-DSP architecture for real-time
embedded systems. It combines microcontroller real-time capability, DSP
computational power and RISC load/store performance in a single core.

The Zephyr port supports two AURIX SoC families:

- `AURIX TC3xx <https://www.infineon.com/products/microcontroller/32-bit-tricore/aurix-tc3xx>`_
  using the TC1.6.2P ISA
- `AURIX TC4x <https://www.infineon.com/products/microcontroller/32-bit-tricore/aurix-tc4x>`_
  using the TC1.8P ISA

Key supported features
**********************

The table below summarizes the status of key OS features across the
supported AURIX SoC families.

.. list-table:: Feature support
   :header-rows: 1
   :widths: 50 25 25

   * - Feature
     - **TC3xx**
     - **TC4x**
   * - ISA version
     - TC1.6.2P
     - TC1.8P
   * - **OS features**
     -
     -
   * - Single-thread kernel support
     - Y
     - Y
   * - Multi-thread kernel support
     - Y
     - Y
   * - Userspace
     - N
     - N
   * - **Interrupt handling**
     -
     -
   * - Regular interrupts (SW ISR table)
     - Y
     - Y
   * - Nested interrupts
     - Y
     - Y
   * - Dynamic interrupts
     - Y
     - Y
   * - Direct interrupts
     - Y
     - Y
   * - CPU idling
     - Y
     - Y
   * - **Memory protection**
     -
     -
   * - HW range-based MPU (DPR/CPR)
     - N
     - N
   * - MPU stack guard
     - N
     - N
   * - **Timer**
     -
     -
   * - System timer (STM)
     - Y
     - Y
   * - **Cache**
     -
     -
   * - Instruction/data cache control
     - Y
     - Y
   * - **Floating point**
     -
     -
   * - Single-precision FPU
     - Y
     - Y
   * - Double-precision FPU
     - N
     - Y
   * - **Multiprocessing**
     -
     -
   * - SMP
     - N
     - N
   * - AMP
     - N
     - N

Architecture
************

Registers
=========

The TriCore architecture provides 32 General Purpose Registers (GPRs) split
into sixteen 32-bit data registers (D[0] through D[15]) and sixteen 32-bit
address registers (A[0] through A[15]). In addition, the Program Counter (PC),
Program Status Word (PSW) and Previous Context Information register (PCXI) form
the core system state.

Several address and data registers have dedicated roles:

- A[10]: Stack Pointer (SP)
- A[11]: Return Address (RA)
- A[0], A[1], A[8], A[9]: Global address registers
- D[15]: Implicit data register
- A[15]: Implicit base address register

The TC1.8P cores of TC4x implement a hypervisor extension and start in
virtualization mode. The boot code drops to the default virtual machine when
the SoC selects :kconfig:option:`CONFIG_CPU_HAS_TRICORE_VIRTUALIZATION`.

Context Save Areas (CSA)
========================

The TriCore architecture saves and restores context in hardware using Context
Save Areas (CSAs). Each CSA is a 64-byte memory block aligned to a 64-byte
boundary. CSAs are linked together to form two lists:

- **Free Context List (FCX)**: Pool of available CSAs
- **Previous Context List (PCX)**: Chain of saved contexts for the current
  execution path

The register set is split into an upper context and a lower context:

- **Upper context** (saved automatically on interrupts, traps and calls):
  A[10]-A[15], D[8]-D[15], PSW and PCXI.
- **Lower context** (saved explicitly with the SVLCX or BISR instructions):
  A[2]-A[7], D[0]-D[7], A[11] and PCXI.

Software therefore does not push and pop registers on context switches,
interrupt entry or function calls.

In Zephyr, the CSA pool is a static array of
:kconfig:option:`CONFIG_TRICORE_CSA_COUNT` entries. The boot code in
:file:`arch/tricore/core/reset.S` links all CSAs into the free list and
programs the FCX and LCX registers. LCX is set
:kconfig:option:`CONFIG_TRICORE_CSA_FCD_RESERVE` entries before the end of the
list, so the free context list depletion (FCD) trap handler has CSAs left to
report the fault and abort the thread.

OS features
***********

Threads
=======

Thread stack alignment
----------------------

Zephyr threads on TriCore use an 8-byte stack alignment
(``ARCH_STACK_PTR_ALIGN = 8``), matching the EABI requirement for the
stack pointer.

Stack pointers
--------------

The TriCore architecture uses a single stack pointer register A[10].
The PSW.IS bit tells whether the processor runs on the thread stack or on
the interrupt stack. On interrupt or trap entry with PSW.IS == 0, the hardware
loads A[10] from the Interrupt Stack Pointer (ISP) register and sets
PSW.IS = 1.

In Zephyr, a single interrupt stack is shared among all exceptions and
interrupts. The size is controlled by :kconfig:option:`CONFIG_ISR_STACK_SIZE`.

Thread context switching
========================

Context switching uses the ``SYSCALL`` instruction. ``arch_switch()`` issues
``syscall 1``, which enters the system call trap handler in supervisor mode.
The handler saves the lower context and calls ``z_tricore_switch()``, which:

- stores the current PCXI, the head of the outgoing thread's CSA chain, in
  the outgoing thread,
- loads the PCXI of the incoming thread,
- returns with FRET, which restores the upper context from the incoming
  thread's CSA chain.

When a thread aborted itself, the kernel hands it over in ``to_reclaim`` and
the handler returns its CSA chain to the free list after the switch.

A context switch is also triggered from interrupt context. ``_isr_wrapper``
asks the scheduler for the next thread after the outermost interrupt returns
(``nested == 0``) and no higher-priority interrupt is pending
(``ICR.PIPN == 0``).

The implementation is in :file:`arch/tricore/core/switch.S`,
:file:`arch/tricore/core/syscall_wrapper.S` and
:file:`arch/tricore/core/isr_wrapper.S`.

Interrupt handling
==================

Interrupt priority
------------------

The TriCore architecture supports up to 255 interrupt priority levels.
Each interrupt source is assigned a Service Request Priority Number (SRPN)
in its Service Request Control (SRC) register of the Interrupt Router (IR).
The ICR (Interrupt Control Register) holds the Current CPU Priority
Number (CCPN) and the Pending Interrupt Priority Number (PIPN).

An interrupt is taken when ``ICR.IE == 1`` and ``PIPN > CCPN``.

On interrupt entry, the hardware:

- saves the upper context to a CSA,
- sets ``PCXI.PIE = ICR.IE`` and ``PCXI.PCPN = ICR.CCPN``,
- sets ``ICR.CCPN = ICR.PIPN`` and ``ICR.IE = 0``,
- loads A[10] from ISP if not already on the interrupt stack,
- jumps to the vector entry at ``BIV + 32 * PIPN``.

Zephyr provides 256 vector entries with 32-byte spacing. Each entry saves the
lower context and jumps to ``z_tricore_prio_isr_<priority>``, a weak alias of
``_isr_wrapper`` unless a direct ISR uses that priority. ``_isr_wrapper`` reads the active interrupt
number from the interrupt router and dispatches through the software ISR
table. ``_isr_wrapper`` re-enables interrupts before calling the handler, so
a higher-priority interrupt can nest.

On return from interrupt (RFE), the hardware restores the upper context and
the previous ICR state from PCXI.

The vectors are in :file:`arch/tricore/core/vectors.c`.

Locking and unlocking IRQs
--------------------------

``arch_irq_lock()`` returns the current ICR value as the key and executes the
``DISABLE`` instruction, which clears ICR.IE. ``arch_irq_unlock()`` executes
``ENABLE`` if ICR.IE was set in the key.

Direct interrupts
-----------------

:c:macro:`IRQ_DIRECT_CONNECT` binds an ISR declared with
:c:macro:`ISR_DIRECT_DECLARE` to the vector of its priority, bypassing the
software ISR table. The priority must be an integer literal and each priority
can have one direct ISR only. The ISR return value requests a reschedule.
``ISR_DIRECT_HEADER()``, ``ISR_DIRECT_FOOTER()`` and ``ISR_DIRECT_PM()`` have
no effect.

Dynamic interrupts
------------------

Interrupts can be connected at run time through the software ISR table with
:kconfig:option:`CONFIG_DYNAMIC_INTERRUPTS`.

CPU idling
==========

``arch_cpu_idle()`` and ``arch_cpu_atomic_idle()`` enable interrupts and
execute ``WAIT``; the core wakes on the next interrupt. An interrupt taken
between ``ENABLE`` and ``WAIT`` returns past the ``WAIT``, so the core does
not sleep after the wake-up event was already served.

The implementation is in :file:`arch/tricore/core/cpu_idle.c`.

Trap handling
=============

The TriCore architecture defines 8 trap classes with 32-byte vector spacing,
addressed via the BTV (Base Trap Vector) register:

- Class 0: MMU traps
- Class 1: Internal protection traps (MPR, MPW, MPX, MPP)
- Class 2: Instruction errors (illegal opcode, privilege violation)
- Class 3: Context management (free context depletion, call depth overflow)
- Class 4: System bus and peripheral errors
- Class 5: Assertion traps
- Class 6: System call (SYSCALL instruction)
- Class 7: Non-Maskable Interrupt (NMI)

Zephyr routes trap classes 0 to 5 and 7 through ``z_tricore_fault()``, which
logs the trap class, the Trap Identification Number (TIN) and a register dump
and then calls ``z_fatal_error()``. A free context list underflow (FCU, class
3 TIN 4) leaves no CSA to handle the trap and halts the core.

The system call trap (class 6) is used by the kernel for context switching
(``syscall 1``), raising exceptions (``syscall 2``) and IRQ offloading
(``syscall 3``).

The implementation is in :file:`arch/tricore/core/fatal.c` and
:file:`arch/tricore/core/vectors.c`.

Userspace
=========

Userspace is not supported. The architecture does not select
``ARCH_HAS_USERSPACE`` and there is no memory protection driver for the
range-based DPR/CPR registers.

Cache
=====

Each core has a program cache and a write-back data cache. Segments 8 and 9
are cached; segments A and B are their non-cached mirrors. Both caches are
bypassed after reset, and Zephyr leaves them bypassed until the application
calls ``sys_cache_instr_enable()`` or ``sys_cache_data_enable()`` with
:kconfig:option:`CONFIG_CACHE_MANAGEMENT`.

The data cache is not coherent with other bus masters. Range operations work
on cache lines; whole-cache operations walk every index and way. The program
cache can only be invalidated as a whole, so a range invalidation invalidates
all of it, and flushing it returns ``-ENOTSUP``.

The implementation is in :file:`arch/tricore/core/cache.c`.

Floating point
==============

The TriCore FPU is integrated into the data path and operates on the
D[0]-D[15] data registers. There are no separate FPU registers. The FPU
rounding mode and exception flags are held in the PSW, which is saved and
restored as part of the upper context on interrupts, traps and calls.

The FPU state is therefore part of every saved context and needs no extra
handling in multi-threaded applications. With
:kconfig:option:`CONFIG_FPU_SHARING`, ``k_float_disable()`` and
``k_float_enable()`` return ``-ENOTSUP``.

- TC3xx (TC1.6.2P): Single-precision FPU (IEEE 754)
- TC4x (TC1.8P): Single-precision and double-precision FPU (IEEE 754)

Toolchain
*********

The TriCore port builds with the AURIX GCC toolchain provided by Infineon,
version 11.3.1:
`Infineon AURIX GCC <https://softwaretools.infineon.com/assets/com.ifx.tb.tool.aurixgcc>`_

.. code-block:: console

   export ZEPHYR_TOOLCHAIN_VARIANT=tricore
   export TRICORE_TOOLCHAIN_PATH=/path/to/tricore-gcc

``TRICORE_TOOLCHAIN_PATH`` defaults to ``/opt/toolchains/tricore-elf``.

The AURIX lite kits also build with a TriCore LLVM toolchain through the
``host/llvm`` variant, which links with LLD:

.. code-block:: console

   export ZEPHYR_TOOLCHAIN_VARIANT=host/llvm
   export LLVM_TOOLCHAIN_PATH=/path/to/tricore-llvm

.. note::

   The Zephyr SDK does not include a TriCore toolchain.

C library
=========

The port uses picolibc, built from the picolibc module unless the Zephyr SDK
is used.

Supported boards
****************

Hardware boards
===============

+----------------------------------------------+----------+------------------+
| Board                                        | SoC      | ISA              |
+----------------------------------------------+----------+------------------+
| :zephyr:board:`kit_a2g_tc375_lite`           | TC375    | TC1.6.2P         |
+----------------------------------------------+----------+------------------+
| :zephyr:board:`kit_a3g_tc4d7_lite`           | TC4D7    | TC1.8P           |
+----------------------------------------------+----------+------------------+

QEMU boards
===========

+----------------------------------------------+----------+------------------+
| Board                                        | SoC      | ISA              |
+----------------------------------------------+----------+------------------+
| :zephyr:board:`qemu_tc3x`                    | TC39x    | TC1.6.2P         |
+----------------------------------------------+----------+------------------+
| :zephyr:board:`qemu_tc4x`                    | TC4Dx    | TC1.8P           |
+----------------------------------------------+----------+------------------+

QEMU for TriCore AURIX is maintained at
`linumiz/qemu-tricore <https://github.com/linumiz/qemu-tricore>`_, which adds
AURIX peripheral emulation.

The following table shows the emulated peripherals used by Zephyr:

.. list-table:: QEMU emulated features
   :header-rows: 1
   :widths: 50 25 25

   * - Feature
     - **qemu_tc3x**
     - **qemu_tc4x**
   * - Interrupt Router (IR)
     - Y
     - Y
   * - UART (ASCLIN)
     - Y
     - Y
   * - System Timer (STM)
     - Y
     - Y
   * - Clock Control (CCU)
     - Y
     - Y
   * - GPIO
     - N
     - N
   * - MPU
     - N
     - N

Drivers
*******

The following table lists the peripheral drivers available in the TriCore port:

+---------------------------+-----------+-----------+
| Driver                    | **TC3xx** | **TC4x**  |
+---------------------------+-----------+-----------+
| UART (ASCLIN)             |     Y     |     Y     |
+---------------------------+-----------+-----------+
| GPIO                      |     Y     |     Y     |
+---------------------------+-----------+-----------+
| System Timer (STM)        |     Y     |     Y     |
+---------------------------+-----------+-----------+
| Clock Control (CCU)       |     Y     |     Y     |
+---------------------------+-----------+-----------+
| Pin Control               |     Y     |     Y     |
+---------------------------+-----------+-----------+
| Interrupt Router (IR)     |     Y     |     Y     |
+---------------------------+-----------+-----------+

GPIO pin interrupts use the external request unit (ERU) of the SCU. The
``infineon,eru-inputs`` property of a port routes a pin to an ERU input and
output channel, and the port's interrupt is the service request of that
output channel. The ERU detects edges only, so level interrupts are not
supported. Pin interrupt configuration returns ``-ENOTSUP`` for level
triggers and for pins that are not listed in ``infineon,eru-inputs``.

Flashing and debugging
**********************

OpenOCD is the recommended flashing flow on Linux. A patched OpenOCD with
TriCore and AURIX support is maintained at:

`linumiz/infineon/release/openocd-aurix <https://gitlab.com/linumiz/infineon/release/openocd-aurix>`_
(default branch ``release/ifx/v1.0.0``).

.. note::

   The TriCore/AURIX support in OpenOCD is maintained in a downstream
   fork and is early in development. It has been validated with the
   boards and operations described here, but corner cases may still
   surface. Issues should be reported to the fork's issue tracker.

Two Infineon vendor packages are required at runtime:

- `DAS (Device Access Server) <https://softwaretools.infineon.com/>`_,
  which provides the ``tas_server`` daemon used by OpenOCD to talk to
  the on-board debugger.
- `libftd2xx <https://ftdichip.com/drivers/d2xx-drivers/>`_ from FTDI,
  which must be placed alongside ``tas_server``.

Build OpenOCD:

.. code-block:: console

   sudo apt install build-essential git autoconf libtool \
                    pkg-config libusb-1.0-0-dev

   git clone https://gitlab.com/linumiz/infineon/release/openocd-aurix.git
   cd openocd-aurix
   ./bootstrap
   mkdir build && cd build
   ../configure --prefix=/opt/tooling/tricore-openocd \
                --enable-tas-client --enable-ftdi \
                --enable-jlink --enable-stlink
   make -j$(nproc) && sudo make install

Install DAS following the package instructions, then copy ``libftd2xx``
into the ``tas_server`` directory.

Add a udev rule so the debugger is accessible without root:

.. code-block:: console

   sudo tee /etc/udev/rules.d/99-infineon-aurix.rules >/dev/null <<'EOF'
   SUBSYSTEM=="usb", ATTRS{idVendor}=="058b", MODE="0666", GROUP="plugdev"
   SUBSYSTEM=="usb", ATTRS{idVendor}=="0403", MODE="0666", GROUP="plugdev"
   EOF
   sudo udevadm control --reload-rules
   sudo udevadm trigger
   sudo usermod -aG plugdev $USER

Log out and back in to pick up group membership.

Start the TAS server once per boot. It listens on TCP port 24817:

.. code-block:: console

   cd <path-to-tas_server>
   LD_LIBRARY_PATH=. ./tas_server &

Once OpenOCD is installed and ``tas_server`` is running, flash with:

.. code-block:: console

   west flash --openocd /opt/tooling/tricore-openocd/bin/openocd \
              --openocd-search /opt/tooling/tricore-openocd/share/openocd/scripts

Each AURIX board passes its OpenOCD configuration from
:file:`support/openocd.cfg` in the board directory. With several boards on
one TAS server, select one with ``adapter serial`` in that file.

Alternative tools
=================

`iSYSTEM winIDEA <https://www.isystem.com/products/software/winidea.html>`_
together with the
`AURIX Development Studio (ADS) <https://softwaretools.infineon.com/assets/com.ifx.tb.tool.aurixide>`_
is a commercial alternative supported on Windows.

A full list of supported flash programming tools is available at:
`Infineon AURIX Flash Tools <https://www.infineon.com/design-resources/platforms/aurix-software-tools/aurix-tools/flash>`_

Architecture references
***********************

- `TC1.8 Architecture Manual Volume 1 <https://www.infineon.com/assets/row/public/documents/10/44/infineon-infineon-tricore-tc1.8-architecture-usermanual-en.pdf>`_
- `TC1.8 Architecture Manual Volume 2 <https://www.infineon.com/assets/row/public/documents/10/44/infineon-tricore-tc1.8-architecture-volume2-usermanual-en.pdf>`_
- `TC1.6 Architecture Manual Volume 1 <https://www.infineon.com/assets/row/public/documents/10/44/infineon-aurix-architecture-vol1-usermanual-en.pdf>`_
- `TC1.6 Architecture Manual Volume 2 <https://www.infineon.com/assets/row/public/documents/10/44/infineon-aurix-architecture-vol2-usermanual-en.pdf>`_

Maintainers and collaborators
*****************************

The status of the TriCore AURIX architecture port in Zephyr is: *maintained*.

The list of maintainers and collaborators can be found in
:file:`MAINTAINERS.yml` under the ``TriCore arch`` and
``Infineon AURIX Platforms`` areas.
