/*
 * Copyright (C) 2013 Mark Hills <mark@xwax.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 */

#include <stdio.h>
#include <string.h>
#include <sys/poll.h>
#include "asoundlib.h"

#include "alsa.h"


/* This structure doesn't have corresponding functions to be an
 * abstraction of the ALSA calls; it is merely a container for these
 * variables. */

struct alsa_pcm {
    snd_pcm_t *pcm;

    struct pollfd *pe;
    size_t pe_count; /* number of pollfd entries */

    signed short *buf;
    snd_pcm_uframes_t period;
    int rate;
};


struct alsa {
    struct alsa_pcm capture, playback;
};


static void alsa_error(const char *msg, int r)
{
    fprintf(stderr, "ALSA %s: %s\n", msg, snd_strerror(r));
}


static int pcm_open(struct alsa_pcm *alsa, const char *device_name,
                    snd_pcm_stream_t stream, int rate, int buffer_time)
{
    int r, dir;
    unsigned int p;
    size_t bytes;
    snd_pcm_hw_params_t *hw_params;
    
    r = snd_pcm_open(&alsa->pcm, device_name, stream, SND_PCM_NONBLOCK);
    if (r < 0) {
        alsa_error("open", r);
        return -1;
    }

    snd_pcm_hw_params_alloca(&hw_params);

    r = snd_pcm_hw_params_any(alsa->pcm, hw_params);
    if (r < 0) {
        alsa_error("hw_params_any", r);
        return -1;
    }
    
    r = snd_pcm_hw_params_set_access(alsa->pcm, hw_params,
                                     SND_PCM_ACCESS_RW_INTERLEAVED);
    if (r < 0) {
        alsa_error("hw_params_set_access", r);
        return -1;
    }
    
    r = snd_pcm_hw_params_set_format(alsa->pcm, hw_params, SND_PCM_FORMAT_S16);
    if (r < 0) {
        alsa_error("hw_params_set_format", r);
        fprintf(stderr, "16-bit signed format is not available. "
                "You may need to use a 'plughw' device.\n");
        return -1;
    }

    r = snd_pcm_hw_params_set_rate(alsa->pcm, hw_params, rate, 0);
    if (r < 0) {
        alsa_error("hw_params_set_rate", r);
        fprintf(stderr, "%dHz sample rate not available. You may need to use "
                "a 'plughw' device.\n", rate);
        return -1;
    }
    alsa->rate = rate;

    r = snd_pcm_hw_params_set_channels(alsa->pcm, hw_params, DEVICE_CHANNELS);
    if (r < 0) {
        alsa_error("hw_params_set_channels", r);
        fprintf(stderr, "%d channel audio not available on this device.\n",
                DEVICE_CHANNELS);
        return -1;
    }

    p = buffer_time * 1000; /* microseconds */
    dir = -1;
    r = snd_pcm_hw_params_set_buffer_time_max(alsa->pcm, hw_params, &p, &dir);
    if (r < 0) {
        alsa_error("hw_params_set_buffer_time_max", r);
        fprintf(stderr, "Buffer of %dms may be too small for this hardware.\n",
                buffer_time);
        return -1;
    }

    p = 2; /* double buffering */
    dir = 1;
    r = snd_pcm_hw_params_set_periods_min(alsa->pcm, hw_params, &p, &dir);
    if (r < 0) {
        alsa_error("hw_params_set_periods_min", r);
        fprintf(stderr, "Buffer of %dms may be too small for this hardware.\n",
                buffer_time);
        return -1;
    }

    r = snd_pcm_hw_params(alsa->pcm, hw_params);
    if (r < 0) {
        alsa_error("hw_params", r);
        return -1;
    }
    
    r = snd_pcm_hw_params_get_period_size(hw_params, &alsa->period, &dir);
    if (r < 0) {
        alsa_error("get_period_size", r);
        return -1;
    }

    bytes = alsa->period * DEVICE_CHANNELS * sizeof(signed short);
    alsa->buf = malloc(bytes);
    if (!alsa->buf) {
        perror("malloc");
        return -1;
    }

    /* snd_pcm_readi() returns uninitialised memory on first call,
     * possibly caused by premature POLLIN. Keep valgrind happy. */

    memset(alsa->buf, 0, bytes);

    return 0;
}


static void pcm_close(struct alsa_pcm *alsa)
{
    if (snd_pcm_close(alsa->pcm) < 0)
        abort();
    free(alsa->buf);
}


static ssize_t pcm_pollfds(struct alsa_pcm *alsa, struct pollfd *pe,
			   size_t z)
{
    int r, count;

    count = snd_pcm_poll_descriptors_count(alsa->pcm);
    if (count > z)
        return -1;

    if (count == 0)
        alsa->pe = NULL;
    else {
        r = snd_pcm_poll_descriptors(alsa->pcm, pe, count);
        if (r < 0) {
            alsa_error("poll_descriptors", r);
            return -1;
        }
        alsa->pe = pe;
    }

    alsa->pe_count = count;
    return count;
}


static int pcm_revents(struct alsa_pcm *alsa, unsigned short *revents) {
    int r;

    r = snd_pcm_poll_descriptors_revents(alsa->pcm, alsa->pe, alsa->pe_count,
                                         revents);
    if (r < 0) {
        alsa_error("poll_descriptors_revents", r);
        return -1;
    }
    
    return 0;
}



/* Start the audio device capture and playback */

static void start(struct device *dv)
{
    struct alsa *alsa = (struct alsa*)dv->local;

    if (snd_pcm_start(alsa->capture.pcm) < 0)
        abort();
}


/* Register this device's interest in a set of pollfd file
 * descriptors */

static ssize_t pollfds(struct device *dv, struct pollfd *pe, size_t z)
{
    int total, r;
    struct alsa *alsa = (struct alsa*)dv->local;

    total = 0;

    r = pcm_pollfds(&alsa->capture, pe, z);
    if (r < 0)
        return -1;
    
    pe += r;
    z -= r;
    total += r;
    
    r = pcm_pollfds(&alsa->playback, pe, z);
    if (r < 0)
        return -1;
    
    total += r;
    
    return total;
}
    

/* Collect audio from the player and push it into the device's buffer,
 * for playback */

static int playback(struct device *dv)
{
    int r;
    struct alsa *alsa = (struct alsa*)dv->local;

    device_collect(dv, alsa->playback.buf, alsa->playback.period);

    r = snd_pcm_writei(alsa->playback.pcm, alsa->playback.buf,
                       alsa->playback.period);
    if (r < 0)
        return r;
        
    if (r < alsa->playback.period) {
        fprintf(stderr, "alsa: playback underrun %d/%ld.\n", r,
                alsa->playback.period);
    }

    return 0;
}


/* Pull audio from the device's buffer for capture, and pass it
 * through to the timecoder */

static int capture(struct device *dv)
{
    int r;
    struct alsa *alsa = (struct alsa*)dv->local;

    r = snd_pcm_readi(alsa->capture.pcm, alsa->capture.buf,
                      alsa->capture.period);
    if (r < 0)
        return r;
    
    if (r < alsa->capture.period) {
        fprintf(stderr, "alsa: capture underrun %d/%ld.\n",
                r, alsa->capture.period);
    }

    device_submit(dv, alsa->capture.buf, r);

    return 0;
}


/* After poll() has returned, instruct a device to do all it can at
 * the present time. Return zero if success, otherwise -1 */

static int handle(struct device *dv)
{
    int r;
    unsigned short revents;
    struct alsa *alsa = (struct alsa*)dv->local;

    /* Check input buffer for timecode capture */
    
    r = pcm_revents(&alsa->capture, &revents);
    if (r < 0)
        return -1;
    
    if (revents & POLLIN) {
        r = capture(dv);
        
        if (r < 0) {
            if (r == -EPIPE) {
                fputs("ALSA: capture xrun.\n", stderr);

                r = snd_pcm_prepare(alsa->capture.pcm);
                if (r < 0) {
                    alsa_error("prepare", r);
                    return -1;
                }

                r = snd_pcm_start(alsa->capture.pcm);
                if (r < 0) {
                    alsa_error("start", r);
                    return -1;
                }

            } else {
                alsa_error("capture", r);
                return -1;
            }
        } 
    }
    
    /* Check the output buffer for playback */
    
    r = pcm_revents(&alsa->playback, &revents);
    if (r < 0)
        return -1;
    
    if (revents & POLLOUT) {
        r = playback(dv);
        
        if (r < 0) {
            if (r == -EPIPE) {
                fputs("ALSA: playback xrun.\n", stderr);
                
                r = snd_pcm_prepare(alsa->playback.pcm);
                if (r < 0) {
                    alsa_error("prepare", r);
                    return -1;
                }

                /* The device starts when data is written. POLLOUT
                 * events are generated in prepared state. */

            } else {
                alsa_error("playback", r);
                return -1;
            }
        }
    }

    return 0;
}


static unsigned int sample_rate(struct device *dv)
{
    struct alsa *alsa = (struct alsa*)dv->local;

    return alsa->capture.rate;
}


/* Close ALSA device and clear any allocations */

static void clear(struct device *dv)
{
    struct alsa *alsa = (struct alsa*)dv->local;

    pcm_close(&alsa->capture);
    pcm_close(&alsa->playback);
    free(dv->local);
}


static struct device_ops alsa_ops = {
    .pollfds = pollfds,
    .handle = handle,
    .sample_rate = sample_rate,
    .start = start,
    .clear = clear
};


/* Open ALSA device. Do not operate on audio until device_start() */

int alsa_init(struct device *dv, const char *device_name,
              int rate, int buffer_time)
{
    struct alsa *alsa;

    alsa = malloc(sizeof *alsa);
    if (alsa == NULL) {
        perror("malloc");
        return -1;
    }

    if (pcm_open(&alsa->capture, device_name, SND_PCM_STREAM_CAPTURE,
                rate, buffer_time) < 0)
    {
        fputs("Failed to open device for capture.\n", stderr);
        goto fail;
    }
    
    if (pcm_open(&alsa->playback, device_name, SND_PCM_STREAM_PLAYBACK,
                rate, buffer_time) < 0)
    {
        fputs("Failed to open device for playback.\n", stderr);
        goto fail_capture;
    }

    device_init(dv, &alsa_ops);
    dv->local = alsa;

    return 0;

 fail_capture:
    pcm_close(&alsa->capture);
 fail:
    free(alsa);
    return -1;
}


/* ALSA caches information when devices are open. Provide a call
 * to clear these caches so that valgrind output is clean. */

void alsa_clear_config_cache(void)
{
    int r;

    r = snd_config_update_free_global();
    if (r < 0)
        alsa_error("config_update_free_global", r);
}
