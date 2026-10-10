# Transcription · 项目概览与开发交接

更新日期：2026-10-09。当前工程版本：1.0.4。
本文记录当前源码、用户已确认的方向和验证边界，供新对话直接接手；不是原始灵感文档的复述，也不是未来功能承诺。

## 1. 项目定位与范围

- 工作区：`i:/理科系列`；本项目：`i:/理科系列/Transcription`。
- 系列 01：`ChemE-Organic Chemistry`，有机化学合成器；系列 02：`ChemE-Entropy`，物理化学效果器。
- 系列 03：`ChemE-Transcription`，生物化学—转录，目标为以基因信息映射声音的减法合成器。（三款自 2026-10-04 起统一加 `ChemE-` 系列前缀）
- 这是原生 JUCE 音频插件，不是网页应用。当前 Windows 产物为 VST3 和 Standalone。
- 第三款是独立同级工程。本版本起三款插件统一改名（`ChemE-` 前缀）并**加入系列安装器**（Windows Inno Setup 三合一安装包，当前版本 1.2.0）；安装器使用独立 AppId，不会卸载旧版已安装插件。改名前没有把第三款加入系列安装器或安装到系统插件目录。
- 原始灵感来源：`C:/Users/echotcxu/WorkBuddy/2026-09-29-11-15-16/理科系列03-生物化学-转录-减法合成器设计.md`。

## 2. 用户已确认的产品方向——后续开发优先遵守

1. **核心体验是“转录正在发生”，不是编辑或编译 ATCG 文本。**
2. 用户直接选择整组密码子，不应被要求逐个输入、循环或拼装碱基。
3. 用户明确认为早期版本按钮太多、操作繁琐；不要默认恢复密集工具栏、原始 DNA 输入框、剪接面板、谐波分析页或大量音色旋钮。
4. 用户已认可当前单场景形态：“非常接近我想要的效果”。优先在此基础上增量改进，而不是重新设计为另一套工作台。
5. **插件内全部用户可见文字必须使用英文**，包括主屏、弹层、菜单、提示、阶段名称和空状态。本中文交接文档及中文沟通不受此限制。
6. 本阶段先关注生物化学转录的交互感，**暂不以音色调校为重点**。音频与动画的进一步关联尚待设计和确认。
7. 此前已选择 MIDI 演奏优先：按键发声、松键释放，允许持续音和 Pad；不能擅自改成扫描到终点就强制结束 MIDI 音符。
8. 涉及核心交互、科学隐喻或演奏方式的重大改动，先与用户对齐；普通缺陷修复和已有方向内的实现可直接推进。

## 3. 当前已实现的界面与操作

- 主屏为一张转录场景，常驻按钮只有：顶部预设、右上更多菜单、底部转录播放按钮；密码子卡片及添加入口是画布内交互区域。
- 主按钮依状态显示 `Start transcription`、`Pause`、`Resume`、`Transcribe again`；空模板时不可播放，空格也可控制播放。
- 场景包含 DNA 双螺旋、`Promoter`、`Terminator`、RNA 聚合酶、转录泡、新生 RNA、合成方向及当前 RNA 长度。
- **DNA 链长度与碱基对一一对应**（v1.0.1）：转录区世界长度 = 碱基数 × 9 px，一个密码子正好三对碱基，双螺旋上的碱基对细线数量严格等于碱基数，每添加一个密码子链身加长 27 px；两端另有 56 px 不画碱基对的双链延伸。短于可视区时整条链居中静止，超出后滚动跟随聚合酶。`Promoter` / `Terminator` 与 5'/3' 端标记跟随链的两端，长基因时 `Terminator` 需滚动或转录到末端才可见（入口 `GeneCanvas::dnaSpan()` / `dnaScroll()` / `drawDna()`）。
- **两侧溢出提示**（v1.0.2）：仅当某端的空白 DNA 尾端（56 px 双螺旋延伸）完全移出面板时才触发淡出——右端条件 `dnaScreenX(span) > panelRight(772)`、左端条件 `dnaScreenX(0) < panelLeft(0)`；淡出固定对齐面板最左/最右边缘（`panelLeft` / `panelRight`），叠加 64 px 白色渐变并画省略号 `…` 提示还有未显示的基因。溢出侧的链身截断到面板边缘，使淡出精确衔接在两端空白 DNA 的边界上、不产生多余空白；该端空白 DNA 滚回面板后淡出消失（入口 `GeneCanvas::drawDna()`）。链的可视窗口为 `dnaStart = 94`、`dnaEnd = 678`，两端各留 56 px 空白双螺旋延伸与 `5'`/`3'` 标记的空间，聚合酶走到最左/最右尽头时该端信息不被裁；碱基对竖线精确裁剪到 `[panelLeft, panelRight]`，不在淡出边缘外残留竖线。
- 播放过程依次表现结合、双链打开、RNA 延伸、RNA 释放及 DNA 重新闭合。完成后停留，不自动循环。
- 直接拖动聚合酶可定位转录进度；拖动通常暂停过程，拖至终点松手会继续完成释放阶段。底部 `Bind / Unwind / Extend / Release` 阶段灯随拖动进度逐渐点亮（播放时按阶段时间点亮）。
- 密码子条使用 RNA 写法（含 U），显示三联体及氨基酸缩写；点击卡片替换整组（点击判定在抬起时，与滑动区分），`+ Add codon` 添加，滚轮或按住拖动浏览长序列，右键点击中间密码子直接删除。
- `CodonPicker` 默认显示 21 个代表性选择（20 种氨基酸各一个密码子，加一个 Stop）；`View all 64 codons` 按需展开完整密码表。
- 选中即应用并关闭弹层；`Remove codon` 明确移除整组。支持方向键、Enter、Escape 和点击弹层外取消。
- 更多菜单包含撤销、重做、重置过程、清空密码子及简短科学说明；撤销/重做也支持 Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y。
- 序列操作撤销历史最多 64 次，不随宿主工程保存；切换工厂预设会清空历史。
- 最多 32 密码子、96 碱基；选择时 U 自动转为 DNA 的 T。旧会话的末尾不完整密码子可通过整组选择补齐。
- 已移除主屏的逐碱基编辑、文本导入框、突变按钮、剪接手柄、双页签、试听键盘和声音旋钮；相关底层能力并未全部删除。

## 4. 必须区分：科学过程、可视化与音频

| 层面 | 当前行为与边界 |
|---|---|
| 科学过程 | 转录是 DNA → RNA，不是蛋白质翻译；读取模板链 3' → 5'，RNA 按 5' → 3' 生长。 |
| 起止标记 | 起始/终止密码子属于翻译语义，不等于转录启动区/终止区；画面两端的区域是示意，不是启动子识别算法。 |
| 可视化 | 沿整个已加载模板生成 RNA，遇到 Stop 仍继续；包含旧静音位置，不模拟剪接后的成熟 RNA。 |
| 音频引擎 | 仍使用首个同框 ATG 到其后首个 Stop 之前作为表达区，这是当前乐器映射，不是完整生物过程仿真。 |
| 二者连接 | 动画播放、暂停、拖动不触发 MIDI、不修改 DNA、不调整声音；**选择密码子会更新 DNA，因而会改变现有 DSP 的谐波模板。** |
| 映射依据 | 氨基酸性质提供设计灵感，增益、Q、失谐及包络数值不是生物分子的真实声学定律。 |

因此，“可视化能完整运行”不代表“一定有声音”：没有同框 ATG 时，现有 DSP 会静音；无 Stop 则表达到完整密码子末尾。后续若改变此规则，应明确处理旧状态和既有 MIDI 演奏选择。

## 5. 视觉规范

- 白底、轻临床风格、低饱和颜色、细线、克制的透明度和留白；沿用前两款的系列语言。
- 默认 820×560，固定比例；可缩放范围 615×420 至 1640×1120。编辑器使用逻辑坐标缩放，画布参考尺寸为 772×382。
- 顶栏保持左侧 `iisaacbeats.cn` 与版本、中央预设、右侧更多入口。
- 主题颜色：正文 `#292D2E`、次要文字 `#858983`、细线 `#E5E7E0`、纸色 `#F7F8F4`、蓝色 `#2A6FB0`、绿色 `#687B54`。
- 碱基颜色：A `#4B504C`、T/U `#B75C55`、C `#497BAF`、G `#B59A49`；不能只靠颜色传达身份，仍保留字母。
- 使用 JUCE 系统字体，没有内嵌字体依赖。英文应尽量简洁，并检查缩小窗口后的长度，不靠增加面板解决文案拥挤。
- `ui::text(const char*)` 内部使用 `juce::String::fromUTF8`。含箭头、乘号、圆点等非 ASCII 符号时，不要直接依赖 JUCE 的窄字符串 ASCII 构造。

## 6. 源码地图与数据流

下表路径均相对于 `i:/理科系列/Transcription`。

| 文件 | 职责 / 主要入口 |
|---|---|
| `Source/PluginEditor.h`、`Source/PluginEditor.cpp` | `TranscriptionAudioProcessorEditor` 与 `CodonPicker`；主屏、菜单、选择库、撤销、窗口同步、30 Hz UI 定时器。 |
| `Source/UI/GeneCanvas.h`、`Source/UI/GeneCanvas.cpp` | 过程状态机、DNA/RNA 绘制、聚合酶拖动、密码子条；`setCodon()`、`removeCodon()`、`toggleRunning()`、`advance()`、`seek()`。 |
| `Source/UI/ScientificLookAndFeel.h` | 颜色、字体、按钮与绘制样式、UTF-8 文本入口；仍保留当前主屏未使用的旋钮绘制。 |
| `Source/Model/Genome.h`、`Source/Model/Genome.cpp` | 固定容量 `Genome`、64 密码子翻译、20 氨基酸属性、`transcribe()`、旧突变/门控能力及 `GenomeMailbox`。 |
| `Source/Dsp/TranscriptionEngine.h`、`Source/Dsp/TranscriptionEngine.cpp` | `transcription::Engine`：噪声、滤波器组、声部、包络、MIDI、平滑与输出保护。 |
| `Source/PluginProcessor.h`、`Source/PluginProcessor.cpp` | `TranscriptionAudioProcessor`：宿主接口、参数、DNA 发布、采样位置 MIDI 路由、状态恢复和工厂预设。 |
| `Source/Parameters.h`、`Source/Parameters.cpp` | 七个 APVTS 参数及音频侧参数读取。 |
| `Source/Presets.h`、`Source/Presets.cpp` | 五个工厂预设，包含序列、来源和声音控制值。 |
| `Tests/DspTests.cpp`、`Tests/StateTests.cpp` | 离线 DSP 测试、原生交互/状态测试，以及截图与音频导出。 |
| `CMakeLists.txt`、`build.ps1` | 依赖版本、插件身份、构建目标及 Windows 构建流程。 |

编辑链路：`CodonPicker` → `GeneCanvas::setCodon()` / `removeCodon()` → `onEdit` → 编辑器 `applyEdit()` → `TranscriptionAudioProcessor::setGenome()` → `GenomeMailbox` → 音频侧 `transcribe()` → `Engine::setTranscript()`。

- Processor 中的 `Genome` 是会话和声音的模型来源；画布持有显示/交互副本，不能让副本与 Processor 脱节。
- `GenomeMailbox` 用原子打包数据和版本检查发布快照，写者由 Processor 串行化；音频读取失败时保持旧快照，不等待模型写锁。
- 当前动画的 `elapsed`、播放状态、滚动和选择是 UI 临时状态；不保存动画进度，不与宿主 BPM/PPQ 同步，也不应被音频线程调用。
- `Phase` 为 `ready`、`binding`、`opening`、`elongating`、`releasing`、`complete`。当前示意时长为结合 0.8 s、打开 0.8 s、延伸至少 1.2 s（每碱基 0.12 s）、释放 1.2 s；不是实测生化速率。

## 7. 已有声音基础与状态兼容

- **三段式 DSP 链路**（v1.0.4）：白噪声 → 启动子门控 → 减法核心（4 级）→ 效果器组（4 级）→ 终止包络 ADSR → 自动响度 → 输出。8 声部。
- **序列分段**：`C[0]=ATG` 启动子门控；`C[1..3]=核心 core`（`coreCount=clamp(N-2,0,3)`）驱动 key1 → 减法核心；`C[4..N-2]=中间 body`（`bodyCount=max(0,N-5)`）驱动 key2 → 效果器组；`C[N-1]=终止密码子` → 终止 ADSR（UAG Amber=pluck / UAA Ochre=pad / UGA Opal=lead，固定）。
- **减法核心 4 级**（入口 `Dsp/SubtractiveCore.*`）：谐波谐振组（32 路带通，能量守恒补偿）→ 频谱倾斜（低通）→ 共振峰（峰值 EQ）→ 谐波偏置（奇偶 + 预热饱和）。参数全部由 key1 映射（`coreResonance/coreHarmonics/coreCutoff/coreSlope/coreFormant/coreFormantAmt/coreOddEven/coreDrive`），无旋钮。
- **效果器组 4 级**（入口 `Dsp/FxChain.*`）：饱和 → 失谐/合唱 → 延迟 → 混响，参数由 key2 映射（`fxDrive/fxDepth/fxTime/fxFeedback/fxSize/fxDamp`）；`bodyCount=0` 时全旁路。
- **全局转录语义**：音色只随全局 `scanPhase ∈ [0,1]` 演化（`GeneCanvas::scanPhase()` → `processor.scanPhase` → `engine.setScanPhase()`）。`ready/binding/opening=0`（纯白噪声），`elongating=progress`，`releasing/complete=1`（完整音色）。激活调度：核心 4 级窗口 ∈ `[0, coreCount/M]`，FX 4 级 ∈ `[coreCount/M, 1]`，逐级 smoothstep；`coreCount=0` 时整条乐音链旁路（纯白噪声，仍带终止 ADSR）。无编辑器时 `scanPhase` 默认 `complete`（完整音色）。
- **自动响度**：静态归一化（`transcript.normalisation`）+ 慢速 AGC（能量追踪，attack 200ms / release 500ms，增益 ±26dB）+ 末端软限幅与 DC 处理。
- MIDI 负责音高与音符时长；支持力度、±2 半音弯音、CC64 踏板、CC7 音量、CC11 表情、CC120/123。
- **旧参数全部降级为 no-op**（v1.0.4）：`precision`/`mutation`/`attackScale`/`releaseScale`/`width`/`output`/`noise` 的 ID 与状态解析保留（宿主会话不崩），但不再驱动 DSP。此项推翻旧「勿删勿改名」约定（见 §9）。

| 参数 ID | 处理（v1.0.4 起） |
|---|---|
| `noise` | 忽略值，恒白噪声 |
| `output` | 忽略值，由自动响度接管 |
| `precision` | 废弃（由 key1 `coreResonance` 接管） |
| `mutation` | 废弃 |
| `width` | 废弃（立体声宽度并入 FX 映射或固定默认） |
| `attackScale` / `releaseScale` | 废弃（终止密码子 ADSR 接管） |

- 状态由 `copyXmlToBinary()` 包装 XML：根 `TranscriptionState`，状态版本 `1`，参数树 `TranscriptionParameters`；`Genome` 保存 `bases` 与逐碱基 `introns`，另存 `program`、`editorWidth`、`editorTab`。
- `editorTab` 仅为旧会话兼容保留，当前没有双页签；恢复旧值不能切回已废弃界面。已打开窗口也能恢复宽度。
- 插件身份：产品 `ChemE-Transcription`（自本版本起加系列前缀；CMake target 仍为 `Transcription`），公司 `iisaacbeats.cn`，Bundle ID `cn.iisaacbeats.ChemETranscription`，Manufacturer Code `Isac`，Plugin Code `CE03`。改名后 VST3 UID 与旧身份不同，与旧插件（若已安装）在宿主中共存。
- 无编辑器时必须仍能恢复 DNA/参数并发声；关闭编辑器不能释放宿主仍按住的音符。动画状态不得成为声音工作的前置条件。

## 8. 工厂预设与来源

| 预设 | 来源 |
|---|---|
| `Central Dogma` | 设计序列，覆盖 20 氨基酸；不是天然基因。 |
| `Insulin / INS` | Homo sapiens，`NM_000207.3`，原记录 nt 60–155。 |
| `Hemoglobin / HBB` | Homo sapiens，`NM_000518.5`，原记录 nt 51–146。 |
| `Fluorescence / GFP` | Aequorea victoria，`M62653.1`，原记录 nt 26–121。 |
| `Start + Stop` | 设计序列 `ATG TGA`；不是转录启动子/终止子模型。 |

三条真实预设于 2026-10-04 对照 NCBI 核验，均为 CDS 前 32 密码子（96 nt）片段，坐标为 1-based inclusive；不是完整基因或天然内含子模型。测试同时核对蛋白翻译，勿把任意随机序列标成真实来源。

## 9. 构建、产物与验证

- 技术栈：C++17、JUCE 8.0.12、CMake ≥3.22、Windows MSVC + Ninja。JUCE 版本固定于 `CMakeLists.txt`。
- 构建命令：`powershell -NoProfile -ExecutionPolicy Bypass -File "i:/理科系列/Transcription/build.ps1" -Jobs 4`。
- 脚本用 `vswhere` 发现 MSVC 和 Windows SDK；最近实际使用 MSVC 14.52.36328、SDK 10.0.28000.0，非硬性固定版本。
- JUCE 优先复用 `i:/理科系列/Organic Chemistry/cmake-build-release-visual-studio/_deps/juce-src`；可用 `-JuceSource` 或 `TRANSCRIPTION_JUCE_SOURCE` 覆盖，通过 `FETCHCONTENT_SOURCE_DIR_JUCE` 传给 CMake。
- 默认目录为项目内 `cmake-build-ninja`，可用 `-BuildDirectory` 指定；纯 DSP 测试可加 `-DspOnly`，建议为其指定不同的绝对构建目录。
- 构建脚本启用测试，并固定 `TRANSCRIPTION_COPY_PLUGIN_AFTER_BUILD=OFF`；成功须同时满足退出码 0 和 `BUILD_OK`，不能只看工具外层的 success 字段。
- Standalone：`cmake-build-ninja/Transcription_artefacts/Release/Standalone/ChemE-Transcription.exe`。
- VST3 包：`cmake-build-ninja/Transcription_artefacts/Release/VST3/ChemE-Transcription.vst3`；安装时使用整个目录包，而非只取内部二进制。
- 日志：`build.log`、最近保留的 `build-console.log`、`cmake-build-ninja/Testing/Temporary/LastTest.log`。
- 后续代码修改后继续自行构建并验证；如果 `ChemE-Transcription.exe` 或宿主锁住产物，提示用户关闭，不擅自结束用户进程，不绕过拒绝。
- macOS 的 AU/VST3/Standalone、macOS 11 最低版本及 x86_64/arm64 配置已写入 CMake，**尚未实际构建验证**。

### 已实际验证的最近基线

- **1.0.1**（2026-10-09）：DNA 链长度动态化 + 默认最小阅读框。新实例默认加载 `Start + Stop` 预设（序列 ATG TGA + 该预设声音参数 + 顶栏名称一致）；每添加一个密码子链身加长三个碱基对。状态测试新增"新实例从最小框架开始"断言；拖拽 scrub 期望改为按新 DNA 世界长度换算；编辑器整组替换测试先加载长工厂序列（默认实例已无中间密码子）。Windows 构建 `BUILD_OK`，两项 CTest 通过；截图 `sequence.png`（默认短链居中）、`transcription.png`（延伸中）、`long-dna.png`（Central Dogma 长链滚动，新增 `--long-snapshot` 导出选项）均已检查。
- **1.0.1 修复**（2026-10-09）：双螺旋的碱基对细线曾按“铺满整条链”绘制，且链长含两端各 88 px 非编码侧翼，导致默认 6 个碱基对画出约 26 条细线。现取消侧翼，转录区长度 = 碱基数 × 9 px，细线数量严格等于碱基数（入口 `GeneCanvas::dnaSpan()` / `drawDna()`）；副作用是默认短链变短（编码区 54 px，含两端延伸共 166 px），每个密码子的增量因此更明显。状态测试新增“拖过整条编码区正好走完全程”断言，scrub 断言改用与链长成比例的位移。Windows 构建 `BUILD_OK`，两项 CTest 通过。
- **1.0.1 修复**（2026-10-09）：拖到最左端触发的聚合酶脱离动画曾把起点写死为 DNA 起点常量；短链居中后两者不再重合，表现为聚合酶先闪现到左侧再抬起。现改为在 `seek()` 触发脱离时记录聚合酶当时的位置（`detachX`），从该位置抬起并缓动漂回起始位点，转录泡则在被松开的位置原地闭合（入口 `GeneCanvas::polymerasePosition()` / `seek()` / `drawDna()`）。状态测试新增两条断言：脱离起点不闪现、动画结束回到未绑定位置时不跳变。Windows 构建 `BUILD_OK`，两项 CTest 通过。
- **1.0.2**（2026-10-09）：长基因序列的两侧溢出提示。仅当某端空白 DNA 尾端（56 px 双螺旋延伸）完全移出面板时触发淡出（右 `dnaScreenX(span) > panelRight`、左 `dnaScreenX(0) < panelLeft`）；淡出固定对齐面板最左/最右边缘（`panelLeft = 0` / `panelRight = 772`），叠加 64 px 白色渐变并画省略号 `…`；溢出侧链身截断到面板边缘，使淡出精确衔接在两端空白 DNA 的边界、不产生多余空白。可视窗口保持 94–678，两端各留 56 px 空白双螺旋延伸与 5'/3' 标记空间（链到尽头时该端信息不被裁）；碱基对竖线精确裁剪到 `[panelLeft, panelRight]`，消除淡出边缘外的竖线残留。版本号由 `project(...)` 同步到 1.0.2。Windows 构建 `BUILD_OK`，两项 CTest 通过。
- **1.0.3**（2026-10-09）：交互与视觉优化。① 密码子条支持按住鼠标拖动水平滑动浏览长序列，点击判定延迟到抬起（位移 < 4 px 视为点击），与原有“点击替换”兼容（入口 `GeneCanvas::mouseDown/Drag/Up`，新增 `codonDragging`/`codonDragMoved`）；② 右键点击中间密码子直接删除（`removeCodon`，起始/终止固定被内部拒绝）；③ 修正终止密码子选择窗口排版——标题/副标题对齐卡片内部、卡片下移避免与副标题重叠（入口 `CodonPicker::paint`/`cellBounds`）；④ 底部 `Bind/Unwind/Extend/Release` 阶段灯随拖动进度逐渐点亮，而非一拖动即跳到 Extend（入口 `TranscriptionAudioProcessorEditor::drawStages`，新增 `GeneCanvas::isDragging()`）；⑤ 提高配色饱和度与对比度，对齐 Organic Chemistry（CPK 元素配色）与 Entropy（载体色）：`muted` 858983→6e6e68、`line` e5e7e0→dcdcd5、`green` 687b54→4e8a3c，碱基色与 `basic` 残基色更鲜明（入口 `ScientificLookAndFeel.h`）。状态测试同步：点击改为按下+抬起，新增右键删除中间密码子、右键起始密码子忽略等断言。版本号同步到 1.0.3。Windows 构建 `BUILD_OK`，两项 CTest 通过。
- **1.0.4 迭代**（2026-10-10）：① 终止密码子选择窗口（`CodonPicker` stop 模式）每个卡片显示三种音头类型与简介——UAA=Pad（Slow swell）、UAG=Pluck（Quick attack）、UGA=Lead（Bright lead），窗口加高、卡片加高以容纳三行（入口 `CodonPicker::paint`/`cellBounds`/`panel`，数据 `stopReleases`/`stopReleaseHints`）。② 编辑密码子不再重置转录：`GeneCanvas::publish()` 与 `setData()` 去掉 `resetProcess()`，改为仅把已走到的进度 clamp 到新总时长内；改密码子（替换/插入/删除/终止）后转录动画保持当前阶段，音色即时变化，无需重新操作一遍转录（预设切换与「Reset transcription」仍显式重置）。
- **1.0.4 修复**（2026-10-10）：pad 等 slow-attack 音色的 delay 回声里仍残留明显「音头」——根因是 `Engine::render()` 的信号链把振幅包络放在效果器组**之后**：`mono = core.process(...); mono = fx.process(...); v = mono * envelope`，于是 delay/混响缓冲里写的是「未加包络」的信号，干音音头虽被 attack 衰减，回声却以满幅度瞬态重现。现把包络乘法移到 `fx.process()` 之前（`mono *= envelope; mono = fx.process(...)`），使 delay/混响回声只保留包络后的信号、平滑跟随 attack。DSP 测试新增 `testEnvelopePrecedesEffects`：pad 音色（attack 0.6s、fxTime 0.09s）的第一次回声落在 attack 窗口内，验证回声到达前后 10ms 的 RMS 无突跳。
- **1.0.4 修复**（2026-10-09）：三种终止密码子音色都有明显「音头」，根因是 AGC 的能量跟踪器 `rmsFollower` 从 0（或静音回落到 0）起步，起音阶段 `gain = targetRms/√0` 被顶到上限 20，`autoGain` 在起音瞬间放大弱信号。现改为 `rmsFollower` 以中性能量（`2·targetRms²`）起步、静音时也回落到该中性能量而非 0，`autoGain` 归 1，消除起音过冲（入口 `Engine::reset()` / `Engine::render()` 静音分支）。
- **1.0.4 修复**（2026-10-09）：自动响度 AGC 曾在无音符（静音）期间把增益顶到上限（`energy=0` → `gain=20`），导致音色变化后的第一次 MIDI 触发爆出大音量。现改为静音时 `autoGain` 回落到中性 1、`rmsFollower` 衰减，仅在有活动 voice 时更新 AGC（入口 `Engine::render()`）。
- **1.0.4**（2026-10-09）：转录三段式 DSP 落地（依开发需求文档）。`transcribe()` 重构为分段模型：`core[3]`/`body`/`stop`，key1 → 减法核心 4 级参数、key2 → 效果器组 4 级参数、终止密码子 → 固定 ADSR（UAG/UAA/UGA → pluck/pad/lead）；删除旧「第 i 密码子 = 第 i 谐波」映射与 GC 派生包络。新增 `Dsp/SubtractiveCore.*`（谐振组 + 倾斜 + 共振峰 + 偏置）与 `Dsp/FxChain.*`（饱和 + 失谐 + 延迟 + 混响）；`Engine`/`Voice` 重写为全局 `scanPhase` 调度（核心/FX 逐级 smoothstep 激活，`core=0` 纯白噪声、`body=0` FX 旁路），删粉/棕噪声，加自动响度（静态归一化 + 慢速 AGC + 软限幅）。`PluginProcessor` 用 `std::atomic<float> scanPhase` 传递，旧 7 参数全部降级 no-op（ID/状态兼容保留）。`GeneCanvas` 输出 `scanPhase` 并新增合成频谱示意（HARMONICS 条）。DSP 测试重写：映射、扫描激活、响度 ±3dB、MIDI/稳定性/分块一致性。版本号同步到 1.0.4。Windows 构建 `BUILD_OK`，两项 CTest 通过。
- 全英文 UI 版本 Windows 构建成功，`BUILD_OK` / exit 0；`TranscriptionDSPTests` 与 `TranscriptionStateTests` 两个 CTest 目标全部通过。
- 改名版本（`ChemE-Transcription`、Bundle ID `cn.iisaacbeats.ChemETranscription`、Plugin Code `CE03`）Windows 构建成功，`BUILD_OK`，两项 CTest 通过；VST3 已纳入系列安装包（`i:/理科系列/dist/iisaacbeats_ScienceSeries_Setup_1.2.0_x64.exe`），系列安装器使用独立 AppId，不会卸载旧版插件。
- DSP 测试覆盖密码表、真实片段翻译、快照一致性、音高、力度、门控、踏板、弯音、声部抢占、分块一致性及极端输入稳定性。
- 状态/交互测试覆盖整密码子选择与删除、64 密码子库、完整转录阶段、暂停/拖动、不由动画触发 MIDI、英文状态按钮、窗口恢复、无 UI 发声、零样本块 Note Off 及损坏状态拒绝。
- 最新 IDE 检查无新增错误/警告；待机、延伸、完成、选择库、全部密码子和缩小窗口的原生截图已检查。
- **未完成或未验证**：实际 DAW 试载、跨宿主兼容、macOS 构建、完整 CPU 性能评估、最终音色/听感验收、发布签名与第三款安装器集成。不能将离线测试通过等同于这些验收通过。

## 10. 预览与回归导出

测试程序：`cmake-build-ninja/TranscriptionStateTests_artefacts/Release/TranscriptionStateTests.exe`。在项目根目录运行，参数为“选项 + 输出文件路径”；推荐相对输出路径，避免 Windows 窄字符命令行损坏中文路径。

| 选项 | 当前对应预览 |
|---|---|
| `--snapshot` | `Preview/sequence.png`，当前单场景待机画面，文件名不是旧页签。 |
| `--process-snapshot` | `Preview/transcription.png`，延伸进度示意。 |
| `--codons-snapshot` | `Preview/codons.png`，紧凑密码子选择库。 |
| `--all-codons-snapshot` | `Preview/all-codons.png`，完整 64 密码子库。 |
| `--complete-snapshot` | `Preview/complete.png`，释放完成。 |
| `--compact-snapshot` | `Preview/compact.png`，615×420 缩小窗口。 |
| `--translation-snapshot` | 兼容选项，与过程截图相同；`Preview/translation.png` 已更新，不代表旧 Translation 页。 |
| `--audio-preview` | `Preview/Transcription-preview.wav`，48 kHz / 24-bit / 立体声，五个预设各 5 秒。 |

截图是原生组件渲染结果，不是设计稿。试听 WAV 来自已有声音引擎，本轮交互与英文文案修改没有产生新的音色设计成果。

## 11. 新对话的接手顺序

1. 先读本文件，再看 `Source/PluginEditor.cpp`、`Source/UI/GeneCanvas.cpp` 和最新 `Preview/transcription.png`；无需重新广泛搜索前两款工程或 JUCE 依赖。
2. 从用户下一条具体需求出发；保留已认可的转录形态、整组密码子选择和全英文语言，不把原始灵感文档里的全部工具重新塞回界面。
3. 若涉及声音与转录过程联动，先确认用户想控制什么，并保持 MIDI gate 与 UI 动画的明确边界；当前没有已批准的联动算法。
4. 增量修改既有文件，保留参数 ID、插件身份和旧状态兼容；不改前两款，不删除工作区 `.codebuddy` 项目数据。
5. 增加针对性回归，运行 `build.ps1`，检查相关英文截图，再如实汇报通过、失败或未验证项；文档单独修改无需重建二进制。
6. `README.md` 仍有早期 ATCG 编辑、双页签、剪接和键盘说明，**不能当作当前交互需求**；以最新用户要求、当前源码及本概览为准。

建议新对话开场：请先阅读 `i:/理科系列/Transcription/PROJECT_OVERVIEW.md`，保留当前全英文、整密码子选择和转录过程交互，在此基础上继续后续开发。
