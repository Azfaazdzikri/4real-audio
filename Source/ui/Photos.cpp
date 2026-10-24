#include "Photos.h"

#include <map>

#include "PhotoData.h"

namespace fourreal::ui
{
const juce::Image& guitarPhoto (const juce::String& code)
{
    static std::map<juce::String, juce::Image> cache;
    auto it = cache.find (code);
    if (it != cache.end())
        return it->second;

    juce::Image img;
    const juce::String file = code + ".jpg";
    for (int i = 0; i < PhotoData::namedResourceListSize; ++i)
        if (file == PhotoData::originalFilenames[i])
        {
            int size = 0;
            if (const char* data = PhotoData::getNamedResource (PhotoData::namedResourceList[i], size))
                img = juce::ImageFileFormat::loadFrom (data, static_cast<size_t> (size));
            break;
        }
    return cache.emplace (code, img).first->second;
}
}
