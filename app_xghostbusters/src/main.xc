#include <platform.h>
#include <xs1.h>
#include "mic_array.h"
#include "app.h"

// Latest-value mailbox keeps slow LCD transfers out of the audio path.
void spectrum_mailbox(chanend input, chanend display) {
    unsigned latest[SPECTRUM_WORDS] = {0};
    unsigned first, request;
    while (1) {
        select {
            case input :> first:
                latest[0] = first;
                for (unsigned i = 1; i < SPECTRUM_WORDS; ++i) input :> latest[i];
                break;
            case display :> request:
                for (unsigned i = 0; i < SPECTRUM_WORDS; ++i) display <: latest[i];
                break;
        }
    }
}

unsafe {
int main(void) {
    chan pcm, spectrum, display;
    par {
        on tile[1]: {
            microphone_init();
            mic_array_start((chanend_t)pcm);
        }
        on tile[0]: audio_capture((unsigned)pcm);
        on tile[0]: spectrum_task(0, (unsigned)spectrum);
        on tile[0]: spectrum_mailbox(spectrum, display);
        on tile[0]: display_task((unsigned)display);
    }
    return 0;
}
}
