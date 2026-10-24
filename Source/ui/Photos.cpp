#include "Photos.h"

#include <map>

#include "PhotoData.h"

namespace fourreal::ui
{
const juce::Image& embeddedImage (const juce::String& file)
{
    static std::map<juce::String, juce::Image> cache;
    auto it = cache.find (file);
    if (it != cache.end())
        return it->second;

    juce::Image img;
    for (int i = 0; i < PhotoData::namedResourceListSize; ++i)
        if (file == PhotoData::originalFilenames[i])
        {
            int size = 0;
            if (const char* data = PhotoData::getNamedResource (PhotoData::namedResourceList[i], size))
                img = juce::ImageFileFormat::loadFrom (data, static_cast<size_t> (size));
            break;
        }
    return cache.emplace (file, img).first->second;
}
}
