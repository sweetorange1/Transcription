#include "GeneCanvas.h"
#include "ScientificLookAndFeel.h"
#include <algorithm>
#include <cmath>

using namespace transcription;
namespace ui = transcription::ui;
namespace
{
constexpr float dnaStart = 94.0f, dnaEnd = 678.0f, dnaY = 112.0f;   // 转录区可视窗口，两端留出空白双螺旋延伸与 5'/3' 标记的空间
constexpr float panelLeft = 0.0f, panelRight = 772.0f;              // 转录动画面板的实际左右边界（画布设计宽 772）
constexpr double bindingSeconds = 0.8, openingSeconds = 0.8, releaseSeconds = 1.2, detachSeconds = 0.55;
constexpr float basePairSpacing = 9.0f;   // 每个碱基对的世界长度：链长随碱基数线性增长
constexpr float dnaMargin = 56.0f;        // 双链在两端的延伸（不属于转录区，也不画碱基对）
const juce::Rectangle<float> codonViewport { 32, 319, 708, 48 };
const juce::Rectangle<float> addBounds { 650, 287, 90, 24 };
float ease(float value) noexcept
{
    const float t = juce::jlimit(0.0f, 1.0f, value);
    return t * t * (3.0f - 2.0f * t);
}
juce::Point<float> bezier(juce::Point<float> a, juce::Point<float> b, juce::Point<float> c, juce::Point<float> d, float t)
{
    const float u = 1.0f - t;
    return a * (u * u * u) + b * (3.0f * u * u * t) + c * (3.0f * u * t * t) + d * (t * t * t);
}
}
GeneCanvas::GeneCanvas()
{
    setName("Transcription scene");
    setTooltip(juce::String::fromUTF8("Click a codon to replace it as a unit. Start (ATG) and stop codons are fixed. Drag RNA polymerase to explore transcription — drag it to the far left to detach it from DNA."));
}
float GeneCanvas::scale() const noexcept { return juce::jmax(0.01f, static_cast<float>(getWidth()) / 772.0f); }
float GeneCanvas::elongationDuration() const noexcept { return juce::jmax(1.2f, static_cast<float>(genome.size) * 0.12f); }
void GeneCanvas::setData(const Genome& next)
{
    Genome normalised = next;
    enforceFrame(normalised);
    if (genome == normalised) return;
    genome = normalised;
    selectedCodon = -1;
    scroll = juce::jlimit(0.0f, maxScroll(), scroll);
    // 编辑密码子后保持转录进度：只把已走到的进度 clamp 到新的总时长内（序列变短时），
    // 不再把转录动画重置回 ready，避免用户改一次音色就要重新操作一遍转录。
    if (started)
        elapsed = juce::jmin(elapsed, bindingSeconds + openingSeconds + elongationDuration() + releaseSeconds);
    processChanged();
}
bool GeneCanvas::setCodon(int index, std::string_view rna)
{
    Genome codon;
    if (!Genome::parse(rna, codon) || codon.size != 3) return false;
    // 起始（0）与终止（最后）密码子固定，仅允许替换中间密码子。
    const int n = genome.codons();
    if (index <= 0 || index >= n - 1 || index >= maxCodons) return false;
    for (int i = 0; i < 3; ++i)
    {
        const auto position = static_cast<size_t>(index * 3 + i);
        genome.bases[position] = codon.bases[static_cast<size_t>(i)];
        genome.silent[position] = false;
    }
    selectedCodon = index;
    scroll = juce::jlimit(0.0f, maxScroll(), static_cast<float>(index) * 68.0f - 320.0f);
    publish();
    return true;
}
bool GeneCanvas::insertCodon(int index, std::string_view rna)
{
    Genome codon;
    if (!Genome::parse(rna, codon) || codon.size != 3) return false;
    // 在终止密码子之前插入：index 落在 1..N-1。
    const int n = genome.codons();
    if (index < 1 || index > n - 1 || genome.size + 3 > maxBases) return false;
    if (!genome.insertCodon(index, codon.bases[0], codon.bases[1], codon.bases[2])) return false;
    selectedCodon = index;
    scroll = juce::jlimit(0.0f, maxScroll(), static_cast<float>(index) * 68.0f - 320.0f);
    publish();
    return true;
}
bool GeneCanvas::setStopCodon(std::string_view rna)
{
    Genome codon;
    if (!Genome::parse(rna, codon) || codon.size != 3) return false;
    const char a = codon.bases[0], b = codon.bases[1], c = codon.bases[2];
    const bool stop = a == 'T' && ((b == 'A' && (c == 'A' || c == 'G')) || (b == 'G' && c == 'A'));
    if (!stop) return false;
    const int n = genome.codons();
    if (n < 1) return false;
    const int index = n - 1;
    genome.bases[static_cast<size_t>(index * 3)] = a;
    genome.bases[static_cast<size_t>(index * 3 + 1)] = b;
    genome.bases[static_cast<size_t>(index * 3 + 2)] = c;
    genome.silent[static_cast<size_t>(index * 3)] = false;
    genome.silent[static_cast<size_t>(index * 3 + 1)] = false;
    genome.silent[static_cast<size_t>(index * 3 + 2)] = false;
    selectedCodon = index;
    publish();
    return true;
}
bool GeneCanvas::removeCodon(int index)
{
    // 起始（0）与终止（最后）密码子固定，不可删除。
    const int n = genome.codons();
    if (index <= 0 || index >= n - 1 || index * 3 >= genome.size) return false;
    const int count = juce::jmin(3, genome.size - index * 3);
    for (int i = 0; i < count; ++i) genome.erase(index * 3);
    selectedCodon = -1;
    scroll = juce::jlimit(0.0f, maxScroll(), scroll);
    publish();
    return true;
}
void GeneCanvas::publish()
{
    if (onEdit) onEdit(genome);
}
void GeneCanvas::processChanged()
{
    if (onProcessChanged) onProcessChanged();
    repaint();
}
void GeneCanvas::resetProcess()
{
    elapsed = 0.0; started = false; running = false; dragging = false;
    processChanged();
}
void GeneCanvas::toggleRunning()
{
    if (genome.size == 0) return;
    if (phase() == Phase::complete) resetProcess();
    started = true;
    running = !running;
    processChanged();
}
GeneCanvas::Phase GeneCanvas::phase() const noexcept
{
    if (detachTime >= 0.0) return Phase::detaching;
    if (!started) return Phase::ready;
    if (elapsed < bindingSeconds) return Phase::binding;
    if (elapsed < bindingSeconds + openingSeconds) return Phase::opening;
    if (elapsed < bindingSeconds + openingSeconds + elongationDuration()) return Phase::elongating;
    if (elapsed < bindingSeconds + openingSeconds + elongationDuration() + releaseSeconds) return Phase::releasing;
    return Phase::complete;
}
float GeneCanvas::progress() const noexcept
{
    if (!started) return 0.0f;
    return juce::jlimit(0.0f, 1.0f, static_cast<float>((elapsed - bindingSeconds - openingSeconds) / elongationDuration()));
}
float GeneCanvas::scanPhase() const noexcept
{
    // 全局转录进度：绑定/解链（尚未延伸）与未开始均为 0，延伸中随 progress 增长，释放/完成恒为 1。
    if (!started || elapsed < bindingSeconds + openingSeconds) return 0.0f;
    return progress();
}
int GeneCanvas::producedBases() const noexcept
{
    return juce::jlimit(0, genome.size, static_cast<int>(std::floor(progress() * static_cast<float>(genome.size) + 0.0001f)));
}
juce::String GeneCanvas::nascentRna() const { return juce::String(genome.rna()).substring(0, producedBases()); }
void GeneCanvas::advance(double seconds)
{
    if (!std::isfinite(seconds) || seconds <= 0.0) return;
    ambientTime = std::fmod(ambientTime + seconds, 600.0);
    if (hintTimer >= 0.0)
    {
        hintTimer -= seconds;
        if (hintTimer < 0.0) hintTimer = -1.0;
    }
    if (detachTime >= 0.0)
    {
        detachTime += seconds;
        if (detachTime >= detachSeconds) detachTime = -1.0;
    }
    const auto previous = phase();
    if (running)
    {
        elapsed = juce::jmin(elapsed + seconds, bindingSeconds + openingSeconds + elongationDuration() + releaseSeconds);
        if (phase() == Phase::complete) running = false;
    }
    if (phase() != previous && onProcessChanged) onProcessChanged();
    repaint();
}
void GeneCanvas::seek(float amount)
{
    if (genome.size == 0 || !std::isfinite(amount)) return;
    if (amount <= 0.0f)
    {
        // 拖到最左端：播放分离动画（聚合酶从 DNA 上脱离，转录泡闭合），
        // 动画结束后回到未绑定（ready）状态。
        if (started && detachTime < 0.0)
        {
            detachX = polymerasePosition().x;   // 在重置进度前记下当前位置，避免聚合酶闪现到别处
            started = false; running = false;
            detachTime = 0.0;
            processChanged();
        }
        return;
    }
    started = true; running = false;
    elapsed = bindingSeconds + openingSeconds + static_cast<double>(juce::jlimit(0.0f, 1.0f, amount)) * elongationDuration();
    processChanged();
}
float GeneCanvas::releaseProgress() const noexcept
{
    return juce::jlimit(0.0f, 1.0f, static_cast<float>((elapsed - bindingSeconds - openingSeconds - elongationDuration()) / releaseSeconds));
}
float GeneCanvas::detachProgress() const noexcept
{
    return detachTime < 0.0 ? 0.0f : juce::jlimit(0.0f, 1.0f, static_cast<float>(detachTime / detachSeconds));
}
float GeneCanvas::dnaSpan() const noexcept
{
    // 转录区的世界长度就是编码区本身：碱基数 × 碱基对间距。因此一个密码子
    // 正好对应三对碱基，每添加一个密码子链身加长三个碱基对；两端另有不画
    // 碱基对的 dnaMargin 延伸。
    return static_cast<float>(genome.size) * basePairSpacing;
}
float GeneCanvas::dnaScroll() const noexcept
{
    const float viewW = dnaEnd - dnaStart;
    const float span = dnaSpan();
    if (span <= viewW) return (span - viewW) * 0.5f;   // 短于可视区：整条链居中静止
    const float worldX = progress() * span;
    return juce::jlimit(0.0f, span - viewW, worldX - viewW * 0.5f);
}
float GeneCanvas::dnaScreenX(float worldX) const noexcept
{
    return worldX - dnaScroll() + dnaStart;
}
float GeneCanvas::bubbleOpenness() const noexcept
{
    if (phase() == Phase::opening) return ease(static_cast<float>((elapsed - bindingSeconds) / openingSeconds));
    if (phase() == Phase::elongating) return 1.0f;
    if (phase() == Phase::releasing) return 1.0f - ease(releaseProgress());
    if (phase() == Phase::detaching) return 1.0f - ease(detachProgress());
    return 0.0f;
}
juce::Point<float> GeneCanvas::polymerasePosition() const noexcept
{
    if (phase() == Phase::detaching)
    {
        // 从聚合酶被松开的那个位置抬起，并缓动回到起始位点，
        // 使动画结束时与未绑定（ready）位置连续。
        const float lift = ease(detachProgress());
        return { juce::jmap(lift, detachX, dnaScreenX(0.0f)), dnaY - 40.0f * lift };
    }
    float approach = 0.0f;
    if (phase() == Phase::binding) approach = ease(static_cast<float>(elapsed / bindingSeconds));
    else if (phase() != Phase::ready) approach = 1.0f;
    const float x = dnaScreenX(progress() * dnaSpan());
    return { x, dnaY - (1.0f - approach) * 40.0f - ease(releaseProgress()) * 33.0f };
}
float GeneCanvas::maxScroll() const noexcept { return juce::jmax(0.0f, static_cast<float>((genome.size + 2) / 3) * 68.0f - codonViewport.getWidth()); }
juce::Rectangle<float> GeneCanvas::codonBounds(int index) const noexcept
{
    return { codonViewport.getX() + static_cast<float>(index) * 68.0f - scroll, 322, 60, 39 };
}
int GeneCanvas::codonAt(juce::Point<float> point) const noexcept
{
    if (!codonViewport.contains(point)) return -1;
    for (int i = 0; i < (genome.size + 2) / 3; ++i) if (codonBounds(i).contains(point)) return i;
    return -1;
}
void GeneCanvas::paint(juce::Graphics& g)
{
    g.addTransform(juce::AffineTransform::scale(scale()));
    const auto enzyme = polymerasePosition();
    const float glow = 0.08f + (running ? 0.035f * static_cast<float>(std::sin(ambientTime * 2.0)) : 0.0f);
    g.setGradientFill(juce::ColourGradient(ui::green.withAlpha(glow), enzyme.x, 128,
                                         juce::Colours::white.withAlpha(0.0f), enzyme.x + 125, 253, true));
    g.fillEllipse(enzyme.x - 180, -22, 360, 300);
    const float span = dnaSpan();
    const float promoterX = dnaScreenX(0.0f);
    const float terminatorX = dnaScreenX(span);
    ui::text(g, "Promoter", { promoterX - 46.0f, 10, 92, 16 }, 9, ui::muted, juce::Justification::centred);
    ui::text(g, "Terminator", { terminatorX - 46.0f, 10, 92, 16 }, 9, ui::muted, juce::Justification::centred);
    g.setColour(ui::line);
    g.drawLine(promoterX, 32, promoterX, 46, 0.8f);
    g.drawLine(terminatorX, 32, terminatorX, 46, 0.8f);
    drawPolymerase(g);
    drawDna(g);
    drawRna(g);
    drawSpectrum(g);
    if (producedBases() == 0)
    {
        const auto hint = genome.size == 0 ? "Start with a codon" : (started ? "The DNA strands are opening" : "Guide RNA polymerase along DNA");
        ui::text(g, hint, { 190, 218, 414, 24 }, 15, ui::muted.withAlpha(0.85f), juce::Justification::centred);
        ui::text(g, genome.size == 0 ? "Choose a codon; the DNA template is built for you." : "Start transcription or drag the polymerase to explore.",
                 { 168, 246, 458, 18 }, 10.5f, ui::muted, juce::Justification::centred);
    }
    drawCodons(g);
}
void GeneCanvas::drawPolymerase(juce::Graphics& g)
{
    const auto p = polymerasePosition();
    const float alpha = genome.size == 0 ? 0.5f : 1.0f - 0.5f * releaseProgress();
    juce::Path body;
    body.startNewSubPath(p.x - 51, p.y - 33);
    body.cubicTo(p.x - 28, p.y - 62, p.x + 51, p.y - 57, p.x + 59, p.y - 14);
    body.cubicTo(p.x + 82, p.y + 14, p.x + 48, p.y + 54, p.x + 16, p.y + 49);
    body.cubicTo(p.x - 16, p.y + 67, p.x - 73, p.y + 40, p.x - 66, p.y + 2);
    body.cubicTo(p.x - 73, p.y - 11, p.x - 64, p.y - 30, p.x - 51, p.y - 33);
    body.closeSubPath();
    g.setColour(ui::green.withAlpha(0.10f * alpha)); g.fillPath(body);
    g.setColour(ui::green.withAlpha((enzymeHovered || dragging ? 0.72f : 0.38f) * alpha));
    g.strokePath(body, juce::PathStrokeType(enzymeHovered || dragging ? 1.5f : 1.0f));
    juce::Path contour;
    contour.startNewSubPath(p.x - 43, p.y - 24);
    contour.cubicTo(p.x - 29, p.y - 43, p.x + 23, p.y - 40, p.x + 40, p.y - 23);
    g.setColour(ui::green.withAlpha(0.18f * alpha)); g.strokePath(contour, juce::PathStrokeType(0.8f));
    ui::text(g, "RNA polymerase", { p.x - 67, p.y + 64, 134, 18 }, 10, ui::green.withAlpha(alpha), juce::Justification::centred);
    if (phase() == Phase::elongating)
    {
        g.setColour(ui::green.withAlpha(0.55f));
        const float x = juce::jmin(728.0f, p.x + 83);
        g.drawArrow({ x - 18, 63, x + 7, 63 }, 0.9f, 4, 4);
    }
}
void GeneCanvas::drawDna(juce::Graphics& g)
{
    const float margin = dnaMargin;
    const float span = dnaSpan();
    // 脱离动画期间聚合酶会漂移回起始位点，但转录泡应在它被松开的位置原地闭合。
    const float centre = phase() == Phase::detaching ? detachX : dnaScreenX(progress() * span);
    const float opening = bubbleOpenness();
    const auto strandY = [centre, opening](float x, bool coding)
    {
        const float distance = (x - centre) / 60.0f;
        const float bubble = std::exp(-distance * distance * distance * distance) * opening;
        const float helix = 18.0f * std::sin((x - 40.0f) * 0.068f);
        return dnaY + (coding ? -1.0f : 1.0f) * (helix * (1.0f - bubble) + 33.0f * bubble);
    };

    // 仅当某端的空白 DNA 尾端（双螺旋延伸）完全移出面板时才触发淡出，
    // 淡出固定对齐面板最左/最右边缘；溢出侧的链身截断到面板边缘，
    // 让淡出渐变精确衔接在两端空白 DNA 的边界上，不产生多余空白。
    const float scrollNow = dnaScroll();
    const float promoterX = dnaScreenX(0.0f);
    const float terminatorX = dnaScreenX(span);
    const bool overflowLeft = promoterX < panelLeft;
    const bool overflowRight = terminatorX > panelRight;
    const float worldLeft = overflowLeft ? (scrollNow - dnaStart) : -margin;                       // 截断到面板左缘（屏幕 0）
    const float worldRight = overflowRight ? (scrollNow + panelRight - dnaStart) : (span + margin); // 截断到面板右缘（屏幕 772）

    // 双链：世界坐标 [worldLeft, worldRight]，随滚动平移。
    juce::Path coding, templateStrand;
    const int segments = static_cast<int>(std::ceil((worldRight - worldLeft) / 2.0f));
    for (int i = 0; i <= segments; ++i)
    {
        const float worldX = worldLeft + (worldRight - worldLeft) * static_cast<float>(i) / static_cast<float>(segments);
        const float x = dnaScreenX(worldX);
        if (i == 0) { coding.startNewSubPath(x, strandY(x, true)); templateStrand.startNewSubPath(x, strandY(x, false)); }
        else { coding.lineTo(x, strandY(x, true)); templateStrand.lineTo(x, strandY(x, false)); }
    }

    // 碱基对：一条细线 = 一个碱基对，数量严格等于碱基数（一个密码子三对），
    // 间距恒为 basePairSpacing，因此链身长度与密码子个数一一对应。
    const int pairs = genome.size;
    for (int i = 0; i < pairs; ++i)
    {
        const float worldX = (static_cast<float>(i) + 0.5f) * basePairSpacing;
        const float x = dnaScreenX(worldX);
        if (x < panelLeft || x > panelRight) continue;   // 精确裁剪到面板边界，避免淡出边缘外残留竖线
        const float a = strandY(x, true), b = strandY(x, false);
        const char letter = genome.bases[static_cast<size_t>(i)];
        const float distance = std::abs(x - centre);
        const bool separated = opening > 0.2f && distance < 46;
        const float alpha = genome.size == 0 ? 0.15f : 0.46f;
        g.setColour(ui::baseColour(letter).withAlpha(alpha));
        if (!separated) g.drawLine(x, a + 2, x, b - 2, 1.3f);
        else
        {
            g.drawLine(x, a, x, a + 8, 1.4f);
            g.drawLine(x, b - 8, x, b, 1.4f);
        }
        g.fillEllipse(x - 1.8f, a - 1.8f, 3.6f, 3.6f);
        g.setColour(ui::baseColour(complement(letter)).withAlpha(alpha));
        g.fillEllipse(x - 1.8f, b - 1.8f, 3.6f, 3.6f);
    }
    g.setColour(ui::ink.withAlpha(0.28f)); g.strokePath(coding, juce::PathStrokeType(1.6f));
    g.setColour(ui::green.withAlpha(0.55f)); g.strokePath(templateStrand, juce::PathStrokeType(1.9f));
    const float leftEnd = dnaScreenX(-dnaMargin);
    const float rightEnd = dnaScreenX(span + dnaMargin);
    ui::text(g, "5'", { leftEnd - 25.0f, 83, 26, 17 }, 10, ui::muted);
    ui::text(g, "3'", { leftEnd - 25.0f, 124, 26, 17 }, 10, ui::green);
    ui::text(g, "3'", { rightEnd + 8.0f, 83, 26, 17 }, 10, ui::muted);
    ui::text(g, "5'", { rightEnd + 8.0f, 124, 26, 17 }, 10, ui::green);
    ui::text(g, "DNA", { 352, 42, 68, 17 }, 10, ui::muted, juce::Justification::centred);
    if (opening > 0.5f && genome.size > 0)
    {
        const int position = juce::jlimit(0, genome.size - 1, producedBases());
        const int start = position / 3 * 3;
        for (int i = 0; i < 3 && start + i < genome.size; ++i)
        {
            const char base = genome.bases[static_cast<size_t>(start + i)];
            const float x = centre - 25.0f + static_cast<float>(i) * 18.0f;
            ui::text(g, juce::String::charToString(base), { x, dnaY - 51, 17, 17 }, 12, ui::baseColour(base), juce::Justification::centred);
            const char paired = complement(base);
            ui::text(g, juce::String::charToString(paired), { x, dnaY + 33, 17, 17 }, 12, ui::baseColour(paired), juce::Justification::centred);
        }
    }

    // 某一侧仍有基因序列没显示时，在该侧面板最边缘做淡出，并用省略号提示还有未显示内容。
    constexpr float fadeWidth = 64.0f;
    if (overflowRight)
    {
        const float fadeLeft = panelRight - fadeWidth;
        g.setGradientFill(juce::ColourGradient(
            juce::Colours::white.withAlpha(0.0f), fadeLeft, dnaY,
            juce::Colours::white, panelRight, dnaY, false));
        g.fillRect(fadeLeft, dnaY - 56.0f, fadeWidth, 112.0f);
        ui::text(g, juce::String::fromUTF8("\xE2\x80\xA6"), { panelRight - 32.0f, dnaY - 13.0f, 24.0f, 20.0f },
                 15.0f, ui::muted, juce::Justification::centred);
    }
    if (overflowLeft)
    {
        const float fadeRight = panelLeft + fadeWidth;
        g.setGradientFill(juce::ColourGradient(
            juce::Colours::white, panelLeft, dnaY,
            juce::Colours::white.withAlpha(0.0f), fadeRight, dnaY, false));
        g.fillRect(panelLeft, dnaY - 56.0f, fadeWidth, 112.0f);
        ui::text(g, juce::String::fromUTF8("\xE2\x80\xA6"), { panelLeft + 8.0f, dnaY - 13.0f, 24.0f, 20.0f },
                 15.0f, ui::muted, juce::Justification::centred);
    }
}
void GeneCanvas::drawRna(juce::Graphics& g)
{
    const int count = producedBases();
    if (count == 0) return;
    const auto enzyme = polymerasePosition();
    const float released = ease(releaseProgress());
    const float span = juce::jmin(410.0f, 90.0f + static_cast<float>(count) * 5.0f);
    const juce::Point<float> tail { juce::jmax(67.0f, enzyme.x - span), 243.0f + released * 6.0f };
    const juce::Point<float> tip { enzyme.x + 6.0f, dnaY + 8.0f + released * 108.0f };
    const juce::Point<float> control1 { tail.x - 25.0f, 192.0f };
    const juce::Point<float> control2 { enzyme.x - 68.0f, 277.0f + released * 8.0f };
    juce::Path rna;
    rna.startNewSubPath(tail);
    rna.cubicTo(control1, control2, tip);
    g.setColour(ui::green.withAlpha(0.60f)); g.strokePath(rna, juce::PathStrokeType(2.0f));
    const auto sequence = nascentRna();
    const bool letters = count <= 42;
    for (int i = 0; i < count; ++i)
    {
        const float t = count > 1 ? static_cast<float>(i) / static_cast<float>(count - 1) : 1.0f;
        const auto point = bezier(tail, control1, control2, tip, t);
        const char base = static_cast<char>(sequence[i]);
        g.setColour(juce::Colours::white); g.fillEllipse(point.x - 3, point.y - 3, 6, 6);
        g.setColour(ui::baseColour(base).withAlpha(0.75f)); g.fillEllipse(point.x - 2, point.y - 2, 4, 4);
        if (letters && (i % 2 == 0 || i == count - 1))
            ui::text(g, juce::String::charToString(base), { point.x - 6, point.y + 5, 12, 13 }, 8.5f, ui::baseColour(base), juce::Justification::centred);
    }
    ui::text(g, "5'", { tail.x - 27, tail.y - 8, 23, 17 }, 10, ui::green);
    ui::text(g, "3'", { tip.x + 10, tip.y - 4, 23, 17 }, 10, ui::green);
    ui::text(g, released > 0.5f ? "RNA released" : "Nascent RNA", { tail.x - 4, 267, 145, 17 }, 10.5f, ui::green);
    ui::text(g, juce::String(count) + " nt", { 654, 244, 84, 22 }, 15, ui::green, juce::Justification::centredRight);
    if (phase() == Phase::elongating && running)
    {
        for (int i = 0; i < 4; ++i)
        {
            const float t = static_cast<float>(std::fmod(ambientTime * 0.6 + static_cast<double>(i) * 0.25, 1.0));
            const float x = enzyme.x + (1.0f - t) * 66.0f;
            const float y = dnaY + 15.0f + (1.0f - t) * 108.0f;
            const char base = "AUCG"[i];
            ui::text(g, juce::String::charToString(base), { x - 6, y - 6, 12, 12 }, 9,
                     ui::baseColour(base).withAlpha(0.16f + t * 0.46f), juce::Justification::centred);
        }
    }
}
void GeneCanvas::drawCodons(juce::Graphics& g)
{
    g.setColour(ui::line); g.drawHorizontalLine(287, 32, 740);
    ui::text(g, "Codon sequence", { 32, 291, 96, 20 }, 10.5f, ui::ink);
    if (hintTimer >= 0.0)
        ui::text(g, "Start codon is fixed and cannot be replaced", { 134, 294, 320, 16 }, 9, ui::muted);
    else
        ui::text(g, "Click to replace · Scroll to browse", { 134, 294, 280, 16 }, 9, ui::muted);
    ui::text(g, genome.size >= maxBases ? "32 / 32" : "+ Add codon", addBounds, 10.5f,
             genome.size >= maxBases ? ui::muted : ui::green, juce::Justification::centredRight);
    {
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(codonViewport.toNearestInt());
        const auto sequence = juce::String(genome.rna());
        const int n = genome.codons();
        for (int i = 0; i < (genome.size + 2) / 3; ++i)
        {
            const auto b = codonBounds(i);
            if (!b.intersects(codonViewport)) continue;
            const auto triplet = sequence.substring(i * 3, i * 3 + 3);
            const bool complete = triplet.length() == 3;
            const char amino = complete ? translate(static_cast<char>(triplet[0]), static_cast<char>(triplet[1]), static_cast<char>(triplet[2])) : '*';
            const auto colour = ui::familyColour(residueInfo(amino).family);
            const bool isStart = i == 0;
            const bool isStop = n > 0 && i == n - 1;
            const bool active = i == selectedCodon || (phase() == Phase::elongating && i == producedBases() / 3);
            g.setColour(active ? ui::green.withAlpha(0.1f) : ((isStart || isStop) ? ui::paper : (i == hoveredCodon ? ui::paper : juce::Colours::white)));
            g.fillRoundedRectangle(b, 4);
            g.setColour(active ? ui::green.withAlpha(0.4f) : ui::line);
            g.drawRoundedRectangle(b, 4, 0.8f);
            ui::text(g, triplet, { b.getX(), b.getY() + 3, b.getWidth(), 18 }, 12, ui::ink, juce::Justification::centred);
            const char* sub = isStart ? "START" : (isStop ? "STOP" : (complete ? residueInfo(amino).abbreviation : "..."));
            ui::text(g, sub, { b.getX(), b.getY() + 22, b.getWidth(), 12 }, 8.2f,
                     (isStart || isStop) ? ui::muted : colour, juce::Justification::centred);
        }
        if (genome.size == 0) ui::text(g, "Choose a codon instead of typing individual bases.", { 34, 326, 500, 28 }, 11, ui::muted);
    }
    if (maxScroll() > 0)
    {
        g.setColour(ui::line); g.fillRoundedRectangle(32, 374, 708, 1.5f, 0.7f);
        const float width = 708.0f * 708.0f / (maxScroll() + 708.0f);
        g.setColour(ui::green.withAlpha(0.35f));
        g.fillRoundedRectangle(32.0f + (708.0f - width) * scroll / maxScroll(), 374, width, 1.5f, 0.7f);
    }
}
void GeneCanvas::drawSpectrum(juce::Graphics& g)
{
    // 合成频谱（非 FFT）：直接取当前谐波模板 + 倾斜/共振峰/偏置的频响，
    // 随 scanPhase 从平坦白噪声逐步长成完整音色谱。
    const auto transcript = transcription::transcribe(genome);
    const float phase = scanPhase();
    const int M = transcript.coreCount + transcript.bodyCount;
    const float coreWindow = M > 0 ? static_cast<float>(transcript.coreCount) / static_cast<float>(M) : 0.0f;
    const float coreProgress = coreWindow > 0.0f ? juce::jlimit(0.0f, 1.0f, phase / coreWindow) : 0.0f;
    const float coreActivation = coreProgress * coreProgress * (3.0f - 2.0f * coreProgress);

    constexpr int bars = 32;
    constexpr float left = 600.0f, top = 150.0f, right = 768.0f, bottom = 194.0f;
    const float step = (right - left) / static_cast<float>(bars);
    constexpr float f0 = 440.0f;
    const float tiltExp = juce::jlimit(0.0f, 3.0f, transcript.coreSlope / 6.0f - 1.0f);

    for (int k = 0; k < bars; ++k)
    {
        const float harmonic = static_cast<float>(k + 1);
        float amp = 0.30f * (1.0f - coreActivation);   // 白噪声平坦基底，随激活衰减
        if (k < transcript.coreHarmonics)
        {
            float h = 1.0f / std::pow(harmonic, tiltExp);
            h *= (k % 2 == 0) ? (1.0f + transcript.coreOddEven) : (1.0f - transcript.coreOddEven);
            const float f = harmonic * f0;
            const float cutoff2 = transcript.coreCutoff * transcript.coreCutoff;
            h *= cutoff2 / (cutoff2 + f * f);
            const float d = (f - transcript.coreFormant) / transcript.coreFormant;
            h *= 1.0f + transcript.coreFormantAmt * 2.0f * std::exp(-d * d);
            amp += h * coreActivation;
        }
        const float height = juce::jlimit(0.0f, 1.0f, amp) * (bottom - top);
        const float x = left + static_cast<float>(k) * step;
        g.setColour(ui::green.withAlpha(0.28f + 0.45f * coreActivation));
        g.fillRect(x + 0.5f, bottom - height, step - 1.0f, height);
    }
    ui::text(g, "HARMONICS", { left, top - 13.0f, right - left, 12 }, 8, ui::muted, juce::Justification::centred);
}
void GeneCanvas::mouseDown(const juce::MouseEvent& event)
{
    // 右键：快速删除密码子（起始与终止固定，removeCodon 内部会拒绝）。
    if (event.mods.isRightButtonDown())
    {
        const auto p = event.position / scale();
        const int hit = codonAt(p);
        if (hit >= 0) removeCodon(hit);
        return;
    }
    if (!event.mods.isLeftButtonDown()) return;
    const auto p = event.position / scale();
    const int n = genome.codons();
    if (addBounds.contains(p) && genome.size < maxBases)
    {
        // + Add codon：在终止密码子之前插入中间密码子。
        pause();
        if (onChooseCodon) onChooseCodon(n - 1, true);
        processChanged(); return;
    }
    // 密码子条：按下进入“可能滑动”状态，是否点击延迟到 mouseUp 再判定。
    if (codonViewport.contains(p))
    {
        codonDragging = true;
        codonDragMoved = false;
        codonDragStart = p;
        codonScrollStart = scroll;
        return;
    }
    const auto enzyme = polymerasePosition();
    if (genome.size > 0 && juce::Rectangle<float>(142, 120).withCentre(enzyme).contains(p))
    {
        dragging = true; dragStart = p; dragStartWorldX = progress() * dnaSpan(); pause();
        setMouseCursor(juce::MouseCursor::DraggingHandCursor); processChanged();
    }
}
void GeneCanvas::mouseDrag(const juce::MouseEvent& event)
{
    const auto p = event.position / scale();
    if (codonDragging)
    {
        // 密码子条水平滑动：超过阈值后按拖动位移平移浏览。
        const float dx = p.x - codonDragStart.x;
        if (std::abs(dx) > 4.0f) codonDragMoved = true;
        if (codonDragMoved)
        {
            scroll = juce::jlimit(0.0f, maxScroll(), codonScrollStart - dx);
            hoveredCodon = -1; repaint();
        }
        return;
    }
    if (!dragging) return;
    seek((dragStartWorldX + (p.x - dragStart.x)) / dnaSpan());
}
void GeneCanvas::mouseUp(const juce::MouseEvent& event)
{
    if (codonDragging)
    {
        codonDragging = false;
        if (!codonDragMoved)
        {
            // 未发生滑动：视为点击密码子，按原有逻辑替换 / 提示。
            const auto p = event.position / scale();
            const int n = genome.codons();
            const int hit = codonAt(p);
            if (hit >= 0)
            {
                selectedCodon = hit; pause();
                if (hit == 0)
                {
                    // 起始密码子固定：仅提示，不打开选择。
                    hintTimer = 2.2;
                }
                else if (hit == n - 1)
                {
                    // 终止密码子：三选一。
                    if (onChooseStop) onChooseStop();
                }
                else
                {
                    // 中间密码子：整组替换。
                    if (onChooseCodon) onChooseCodon(hit, false);
                }
                processChanged();
            }
        }
        return;
    }
    if (!dragging) return;
    dragging = false;
    if (progress() >= 1.0f) running = true;
    setMouseCursor(juce::MouseCursor::PointingHandCursor); processChanged();
}
void GeneCanvas::mouseMove(const juce::MouseEvent& event)
{
    if (codonDragging) return;   // 密码子条滑动中不更新悬停
    const auto p = event.position / scale();
    hoveredCodon = codonAt(p);
    enzymeHovered = genome.size > 0 && juce::Rectangle<float>(142, 120).withCentre(polymerasePosition()).contains(p);
    const bool clickable = hoveredCodon >= 0 || enzymeHovered || (addBounds.contains(p) && genome.size < maxBases);
    setMouseCursor(clickable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
}
void GeneCanvas::mouseExit(const juce::MouseEvent&)
{
    hoveredCodon = -1; enzymeHovered = false; repaint();
}
void GeneCanvas::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (event.position.y / scale() < 285) return;
    scroll = juce::jlimit(0.0f, maxScroll(), scroll - (wheel.deltaX + wheel.deltaY) * 330.0f);
    hoveredCodon = -1; repaint();
}
