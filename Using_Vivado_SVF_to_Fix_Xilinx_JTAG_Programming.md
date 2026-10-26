# Using Vivado SVF to Fix the Xilinx JTAG Programming Sequence

## 1. Objective

The objective was to program the Xilinx Zynq UltraScale+ MPSoC (`XCZU7EV`) on an Avnet UltraZed-EV SOM using `openFPGALoader` from an STM32MP257 processor.

The STM32MP257 uses GPIO bit-banging to drive JTAG signals. The existing `stm32mp2-gpio` cable driver was reused.

Programming initially failed to complete the FPGA configuration correctly. We investigated the programming sequence used by Vivado and compared it with the sequence implemented in openFPGALoader.

## 2. Hardware and Software

- **FPGA:** Xilinx Zynq UltraScale+ MPSoC, XCZU7EV
- **Board:** Avnet UltraZed-EV SOM
- **Host processor:** STM32MP257
- **Programming software:** openFPGALoader
- **Reference programmer:** Xilinx Vivado Hardware Manager, version 2022.2
- **JTAG interface:** STM32MP257 GPIO bit-banging

The same STM32MP257, carrier board, PCB wiring, and GPIO bit-banging driver are also used to program an Intel Arria-10 SOM.

## 3. Identifying the JTAG Chain

The JTAG scan identified two devices:

| Device | IDCODE | Function |
|---|---|---|
| Zynq UltraScale+ PL | `0x04730093` | FPGA programmable logic |
| ARM DAP | `0x5BA00477` | ARM debug access port |

The implementation configures the chain with the PL device active and the ARM DAP in BYPASS.

The working chain configuration uses:
- PL instruction-register length: **12 bits**
- ARM DAP instruction-register length: **4 bits**

The PL IR length of 12 bits was identified during the SVF investigation and recorded in the implementation as SVF-confirmed.

## 4. Using Vivado as the Reference

Vivado successfully programmed the same FPGA through the board's four-pin JTAG connector.

We attempted to generate an SVF file using Vivado 2022.2 to inspect the programming operations.

The initial command was:

```tcl
write_hw_svf -force vivado_program.svf
```

Vivado reported an argument-parsing error involving the selected hardware target, `xczu7_0`. This indicated that the command syntax and selected target needed further investigation.

The purpose of generating the SVF was to obtain a reference sequence of JTAG operations, including instruction-register operations, data-register transfers, and configuration-related commands.

**Note:** The exact successful SVF-generation command and the relevant SVF excerpts should be added when the final working file and command are available.

## 5. Applying the Findings to openFPGALoader

The investigation focused on making the openFPGALoader sequence consistent with the Zynq UltraScale+ JTAG chain and configuration requirements.

The implementation was updated to explicitly represent the chain:

1. Identify the PL device and ARM DAP.
2. Select the PL device for configuration.
3. Account for the ARM DAP using BYPASS.
4. Use the 12-bit PL instruction register and 4-bit ARM DAP instruction register.
5. Perform configuration and startup operations.
6. Read the configuration status register to verify completion.

The status checks include `STAT` reads before and after the startup operation (`JSTART`).

These reads report the configuration state, including initialization, DONE, end-of-startup, and startup-state fields.

## 6. Verifying Configuration

The FPGA configuration status was used to distinguish a successful programming operation from a failed one.

A successful run reported:

```text
CRC Error       No CRC error
EOS             0x1
INIT_B_INTERNAL Initialization finished
INIT_B_PIN      0x1
DONE_INTERNAL   DONE internally released
DONE_PIN        DONE pin High
ID Error        No ID error
DEC Error       0x0
STARTUP State   0x4
```

The SOM's blue DONE LED was also illuminated after successful programming.

When the JTAG frequency was increased beyond the reliable operating range, the status instead showed:

```text
EOS             0x0
DONE_INTERNAL   DONE internally held Low
DONE_PIN        DONE pin Low
STARTUP State   0x0
```

This indicates that configuration had not completed successfully.

## 7. Final GPIO Timing

For reliable Zynq UltraScale+ programming, the GPIO bit-banging implementation uses a software delay loop with 12 `nop` instructions. The JTAG TCK frequency should be kept **below 5 MHz for safety**.

```cpp
void Stm32mp2JtagBitbang::wait_half_period()
{
    /*
     * TODO:
     * Add or remove NOPs to adjust JTAG TCK frequency,
     * based on processor speed and PCB trace routing.
     * Keep TCK below 5 MHz for safety.
     */
    volatile int nop_delay;
    for (nop_delay = 0; nop_delay < 12; nop_delay++) {
        asm volatile("nop");
    }
}
```

The delay is processor-dependent and does not guarantee an exact TCK frequency. Verify the actual frequency using an oscilloscope or logic analyzer.

The conservative timing is intended to prioritize reliable configuration over maximum programming speed. With an earlier two-`nop` timing implementation, a requested frequency of 10 MHz produced a measured TCK frequency of approximately 5.75 MHz and successful configuration in approximately 27.38 seconds. Tests above approximately 6 MHz failed to complete configuration. The current 12-`nop` implementation is intended to keep TCK below 5 MHz; measure the actual frequency and verify DONE after programming.

## 8. Building and Using openFPGALoader

### 8.1 Configure and Compile

Configure the project for the STM32MP2 GPIO cable using the Buildroot toolchain file:

```bash
cmake -S . -B build-stm32mp2/ \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/stm32mp2-buildroot.cmake" \
  -DENABLE_CABLE_ALL=OFF \
  -DENABLE_STM32MP2GPIO=ON \
  -DCMAKE_BUILD_TYPE=Release
```

Compile the project:

```bash
cmake --build build-stm32mp2/ -j"$(nproc)"
```

The resulting executable is expected at `build-stm32mp2/openFPGALoader`. Run the following commands from the repository root, adjusting the executable or bitstream path if your build layout differs.

### 8.2 Program Without Debug Output

```bash
./build-stm32mp2/openFPGALoader \
  --freq 5000000 \
  -c stm32mp2-gpio \
  fpga/zync/Base_Zynq_MPSoC_wrapper.bit
```

This uses a requested TCK frequency below 5 MHz. The requested frequency is not a guarantee of the actual TCK frequency; measure TCK on the hardware.

### 8.3 Program With Verbose Debug Output

Use `--verbose-level 2` to enable the diagnostic output used during this investigation:

```bash
./build-stm32mp2/openFPGALoader \
  --verbose-level 2 \
  --freq 5000000 \
  -c stm32mp2-gpio \
  fpga/zync/Base_Zynq_MPSoC_wrapper.bit
```

The debug output can include JTAG chain detection and configuration status, such as `STAT`, `DONE_PIN`, `EOS`, and `STARTUP State`.

### 8.4 Optional Reset

To request a reset after programming, add `--reset`:

```bash
./build-stm32mp2/openFPGALoader \
  --verbose-level 2 \
  --freq 5000000 \
  -c stm32mp2-gpio \
  fpga/zync/Base_Zynq_MPSoC_wrapper.bit \
  --reset
```

Confirm successful configuration by checking that `DONE_PIN` is High, `EOS` is `0x1`, `STARTUP State` is `0x4`, and the SOM DONE LED is illuminated.

## 9. Conclusion

The investigation used Vivado as the reference programmer to understand the Zynq UltraScale+ JTAG chain and configuration sequence.

The resulting openFPGALoader implementation explicitly handles the PL device and ARM DAP chain, uses the 12-bit PL instruction register, and verifies configuration through the FPGA's status register.

The current STM32MP257 GPIO bit-banging implementation prioritizes reliable programming at a conservative TCK frequency.

## 10. TODO: Future Improvements

- **Investigate the JTAG buffer between the STM32MP257 MPU and the SOM.** Replacing or optimizing the buffer may allow a higher TCK frequency while maintaining reliable FPGA configuration.

## 11. Items to Complete

- Record the exact Vivado command that successfully generated the SVF.
- Include relevant SVF excerpts and explain the corresponding openFPGALoader changes.
- Identify the exact source-code changes directly derived from the SVF.
- Record the final successful openFPGALoader commit or patch.
