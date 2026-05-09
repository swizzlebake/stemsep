#include <catch2/catch_session.hpp>
#include <juce_events/juce_events.h>

int main(int argc, char* argv[])
{
    // JUCE requires a MessageManager on the thread that creates AudioProcessor objects.
    auto* mm = juce::MessageManager::getInstance();
    mm->setCurrentThreadAsMessageThread();

    int result = Catch::Session().run(argc, argv);

    juce::MessageManager::deleteInstance();
    return result;
}
