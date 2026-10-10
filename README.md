# Transcription · 生物化学 — 转录

理科系列 03，版本 1.0.1。独立 JUCE 8.0.12 / C++17 噪声减法合成器；不修改 Organic Chemistry 或 Entropy。

## 构建与试用

Windows 需要 CMake 3.22+、Ninja、Visual Studio C++ 工具和 Windows SDK。

在 PowerShell 中执行 `powershell -ExecutionPolicy Bypass -File "i:/理科系列/Transcription/build.ps1"`。脚本自动寻找 MSVC，优先复用同级 Organic Chemistry 的 JUCE 源码，构建后执行 CTest，成功输出 `BUILD_OK`，不向系统插件目录复制文件。

- 独立版：`cmake-build-ninja/Transcription_artefacts/Release/Standalone/Transcription.exe`
- VST3：`cmake-build-ninja/Transcription_artefacts/Release/VST3/Transcription.vst3`，安装时复制整个包目录。
- 只构建离线 DSP 测试：为脚本增加 `-DspOnly -BuildDirectory cmake-build-dsp`。
- 可用 `-JuceSource` 指定 JUCE 路径，或设置 `TRANSCRIPTION_JUCE_SOURCE`。
- macOS 的 CMake 配置包含 AU、VST3、Standalone；此版本尚未在 macOS 或实际 DAW 中验证。

## 使用

- MIDI 按键发声、松键释放；右侧小键盘可试听。8 声部，支持力度、±2 半音弯音、CC64 踏板、CC7 音量、CC11 表情、CC120/123。
- 左键碱基循环 A → T → C → G，右键删除，链尾 `+` 追加底部选定的碱基。
- `EDIT DNA` 接受 A/T/C/G 或 RNA 的 U，允许空格与换行；最多 96 碱基，超长或非法文本会拒绝而非截断。替换文本会清除剪接标记。
- 底部剪接条拖出内含子；点击已有内含子选中，再拖两端手柄调整范围；右键恢复外显子。Shift+点击密码子也可门控。
- `TRANSLATION` 查看 RNA、谐波模板和实际序列包络；点击谐波条切换门控。图表不是实时 FFT。
- `MUTATE` 做一次点突变，保护原始起止密码子；`Mutation` 同时调节突变概率和轻微慢速失谐。CC1 为 His 残基加入额外运动。
- `UNDO` / `REDO` 或 Ctrl+Z / Ctrl+Shift+Z 撤销序列、突变与剪接；最多保留 64 次序列操作，切换工厂预设后重置历史。
- 所有声音旋钮及噪声颜色均可宿主自动化。DNA、剪接、参数、窗口宽度和页签随宿主工程保存；没有单独的用户预设文件浏览器。

## 映射边界

白／粉／棕噪声是唯一的音频激励源，没有混入定音振荡器。最多 32 个高 Q 带通共振位置跟踪 MIDI，超出安全 Nyquist 范围的谐波被静音；滤波参数和增益平滑更新，输出有安全软拐点。

使用标准遗传密码：64 密码子、20 氨基酸、3 终止密码子。每 3 个碱基从第 1 位起固定分组，第 n 个密码子对应第 n 谐波。第一个同框 ATG 到之后首个 TAA/TAG/TGA 之前为表达区；没有 ATG 则静音，没有 Stop 则表达至完整密码子末尾。末尾不足 3 个碱基不表达。

本版剪接是对原始谐波位置的门控，不重新拼接阅读框，也不会移除起止标记的边界作用。包络由序列 GC 比例、表达数量及残基性质生成；Attack/Release 只提供 0.25–4 倍微调。起止标记不启动扫描计时，也不代替 MIDI Note Off。

氨基酸结构、疏水性、带电性质等提供设计灵感；增益、Q、奇偶倾向及包络数值是乐器设计映射，不是生物分子的真实声学定律。DNA A/T 配对有两条氢键，G/C 配对有三条氢键，并非共价双键／三键。

## 工厂序列来源

真实预设仅使用编码区前 32 个密码子的片段，不代表完整基因、完整蛋白或天然基因组内含子；来源在预设提示中保留。序列于 2026-10-04 核对，测试同时核对对应蛋白翻译。

| 预设 | 记录 | 原记录坐标，1-based inclusive |
|---|---|---|
| Insulin / INS | [NM_000207.3](https://www.ncbi.nlm.nih.gov/nuccore/NM_000207.3)，Homo sapiens | 60–155，CDS 前 96 nt |
| Hemoglobin / HBB | [NM_000518.5](https://www.ncbi.nlm.nih.gov/nuccore/NM_000518.5)，Homo sapiens | 51–146，CDS 前 96 nt |
| Fluorescence / GFP | [M62653.1](https://www.ncbi.nlm.nih.gov/nuccore/M62653.1)，Aequorea victoria | 26–121，CDS 前 96 nt |

`Central Dogma` 是涵盖 20 氨基酸的设计序列；`Start + Stop` 是 ATG/TGA 的最小设计序列。两者不宣称来自真实基因。

## 验证与预览

- `TranscriptionDSPTests`：密码表、真实序列翻译、并发快照、音高、力度、边界／剪接静音、踏板、弯音、极端采样率及参数稳定性。
- `TranscriptionStateTests`：状态往返、不开 UI 的音频恢复、样本级 MIDI、旁路、损坏状态、关闭编辑器不中断宿主音符、窗口恢复及原生界面渲染。
- `Preview/sequence.png`、`Preview/translation.png` 为原生组件截图。
- `Preview/Transcription-preview.wav` 为 48 kHz / 24-bit 立体声试听：五个预设依次各 5 秒，前三秒持音、之后释放。顺序为 Central Dogma、INS、HBB、GFP、Start + Stop。

测试验证的是可复现的工程行为；最终音色、CPU 占用和宿主兼容性仍需在实际音乐工程里试听与验证。
