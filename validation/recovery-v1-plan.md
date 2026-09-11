# Bounded Lookahead Recovery v1

Scope: Spectral 3 High and above. Keep latency, parameters, dry mix, min-gain window and FIR detector unchanged. No installation in this task.

Design: maintain the existing slow release and a faster release candidate. Admit no more than 0.5 dB of recovered attenuation, bounded by the existing minimum-safe-gain window. Suppress recovery according to low-band energy share and retain cycle protection. Adaptive Release zero retains the previous output. This is not makeup gain or upward compression.

- [x] Add paired baseline/candidate test for short peak recovery, low-frequency material, independent reconstruction, and Adaptive zero.
- [x] Observe missing-recovery failure before editing DSP.
- [x] Implement bounded release recovery without allocation or new controls.
- [x] Run paired metrics and existing latency/peak/mix tests, build Release VST3, run pluginval strictness 10.
- [x] Report measured benefit, CPU cost, limitations and artifact hash (recovery-v1-results.md).

Adoption gate: at least 0.1 dB RMS recovery on the synthetic short-peak fixture, no sample ceiling violation, independent reconstructed peaks within existing test tolerance, no latency or dry endpoint change. Do not claim guaranteed LUFS increase on arbitrary masters or subjective improvement without listening evidence.
