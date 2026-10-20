#pragma once
namespace transcription
{
// 实时输出频谱的显示约定（Processor 计算，UI 读取）。
inline constexpr int spectrumBandCount = 96;
inline constexpr float spectrumMinDb = -60.0f;
inline constexpr float spectrumMaxDb = 0.0f;
}
