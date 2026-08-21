#include "TracksContainer.h"

int TracksContainer::computeTotalHeight() const
{
    if (tracksPtr == nullptr || tracksPtr->empty())
        return 0;

    auto count = (int) tracksPtr->size();
    auto total = count * collapsedHeight;

    if (expandedIndex >= 0 && expandedIndex < count)
        total += expandedHeight - collapsedHeight;

    return total;
}

void TracksContainer::resized()
{
    if (tracksPtr == nullptr)
        return;

    int y = 0;

    for (size_t i = 0; i < tracksPtr->size(); ++i)
    {
        auto h = ((int) i == expandedIndex) ? expandedHeight : collapsedHeight;
        (*tracksPtr)[i]->setBounds (0, y, getWidth(), h);
        y += h;
    }
}
