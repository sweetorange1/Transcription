#pragma once
#include <JuceHeader.h>
#include "Model/Genome.h"
#include <cmath>

namespace transcription::ui
{
inline const juce::Colour ink { 0xff292d2e }, muted { 0xff6e6e68 }, line { 0xffdcdcd5 };
inline const juce::Colour paper { 0xfff7f8f4 }, blue { 0xff2a6fb0 }, green { 0xff4e8a3c };
inline juce::Colour baseColour(char base)
{
    // 参考 Organic Chemistry 的 CPK 元素配色与 Entropy 的载体色，提高饱和度。
    switch (base)
    {
        case 'T': case 'U': return juce::Colour(0xffb0493c);
        case 'C': return juce::Colour(0xff3a78c2);
        case 'G': return juce::Colour(0xffc18d26);
        default: return juce::Colour(0xff3c403c);
    }
}
inline juce::Colour familyColour(Family family)
{
    switch (family)
    {
        case Family::polar: return blue;
        case Family::acidic: return baseColour('T');
        case Family::basic: return juce::Colour(0xff8764a6);
        case Family::stop: return muted;
        default: return green;
    }
}
inline void text(juce::Graphics& g, const juce::String& value, juce::Rectangle<float> bounds,
                 float size = 11.0f, juce::Colour colour = ink, int alignment = juce::Justification::centredLeft)
{
    g.setFont(juce::Font(juce::FontOptions(size)));
    g.setColour(colour);
    g.drawText(value, bounds, alignment, false);
}
inline void text(juce::Graphics& g, const char* value, juce::Rectangle<float> bounds,
                 float size = 11.0f, juce::Colour colour = ink, int alignment = juce::Justification::centredLeft)
{
    text(g, juce::String::fromUTF8(value), bounds, size, colour, alignment);
}
class ScientificLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    float scale = 1.0f;
    ScientificLookAndFeel()
    {
        setColour(juce::Slider::textBoxTextColourId, ink);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::thumbColourId, green);
        setColour(juce::TextEditor::textColourId, ink);
        setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
        setColour(juce::TextEditor::outlineColourId, line);
        setColour(juce::TextEditor::focusedOutlineColourId, blue);
        setColour(juce::TextEditor::highlightColourId, blue.withAlpha(0.18f));
        setColour(juce::PopupMenu::backgroundColourId, juce::Colours::white);
        setColour(juce::PopupMenu::textColourId, ink);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, paper);
        setColour(juce::PopupMenu::highlightedTextColourId, blue);
        setColour(juce::TooltipWindow::backgroundColourId, juce::Colours::white);
        setColour(juce::TooltipWindow::textColourId, ink);
        setColour(juce::TooltipWindow::outlineColourId, line);
    }
    juce::Font getTextButtonFont(juce::TextButton& button, int) override
    {
        const bool isPreset = static_cast<bool>(button.getProperties().getWithDefault("preset", false));
        return juce::Font(juce::FontOptions((isPreset ? 17.0f : 11.0f) * scale));
    }
    juce::Font getLabelFont(juce::Label&) override { return juce::Font(juce::FontOptions(11.5f * scale)); }
    juce::Font getPopupMenuFont() override { return juce::Font(juce::FontOptions(13.0f * scale)); }
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&, bool hover, bool down) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f * scale);
        const bool flat = static_cast<bool>(button.getProperties().getWithDefault("flat", false));
        const auto accent = button.findColour(juce::TextButton::textColourOnId);
        const bool selected = button.getToggleState();
        if (flat && !hover && !down && !selected) return;
        g.setColour(selected ? accent.withAlpha(0.085f) : paper);
        g.fillRoundedRectangle(bounds, 3.0f * scale);
        if (!flat)
        {
            g.setColour(selected ? accent.withAlpha(0.4f) : line);
            g.drawRoundedRectangle(bounds, 3.0f * scale, scale);
        }
    }
    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool hover, bool) override
    {
        const bool nucleotide = static_cast<bool>(button.getProperties().getWithDefault("nucleotide", false));
        const auto colour = button.getToggleState() || nucleotide ? button.findColour(juce::TextButton::textColourOnId) : (hover ? blue : ink);
        g.setFont(getTextButtonFont(button, button.getHeight()));
        g.setColour(colour.withAlpha(button.isEnabled() ? 1.0f : 0.3f));
        g.drawText(button.getButtonText(), button.getLocalBounds().reduced(3), juce::Justification::centred, false);
    }
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float value,
                          float start, float end, juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                                  static_cast<float>(width), static_cast<float>(height)).reduced(8.0f * scale);
        const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.45f;
        const auto centre = bounds.getCentre();
        const auto accent = slider.findColour(juce::Slider::thumbColourId);
        const float angle = start + value * (end - start);
        g.setColour(juce::Colour(0xffd8d8d1));
        for (int i = 0; i < 5; ++i)
        {
            const float a = start + static_cast<float>(i) * 0.25f * (end - start);
            g.drawLine(centre.x + std::sin(a) * (radius + 4.0f * scale), centre.y - std::cos(a) * (radius + 4.0f * scale),
                       centre.x + std::sin(a) * (radius + 7.0f * scale), centre.y - std::cos(a) * (radius + 7.0f * scale), scale);
        }
        juce::Path arc;
        arc.addCentredArc(centre.x, centre.y, radius + 2.0f * scale, radius + 2.0f * scale, 0.0f, start, angle, true);
        g.setColour(accent);
        g.strokePath(arc, juce::PathStrokeType(2.0f * scale));
        g.setColour(juce::Colour(0xfff7f7f3));
        g.fillEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre));
        g.setColour(juce::Colour(0xffd2d2cb));
        g.drawEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre), scale);
        g.setColour(accent);
        g.drawLine(centre.x, centre.y, centre.x + std::sin(angle) * (radius - 2.5f * scale),
                   centre.y - std::cos(angle) * (radius - 2.5f * scale), 2.0f * scale);
    }
};
}
