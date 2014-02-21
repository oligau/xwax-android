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
#include <asoundlib.h>

#include "tinyalsa.h"


/* This structure doesn't have corresponding functions to be an
 * abstraction of the ALSA calls; it is merely a container for these
 * variables. */

struct alsa_pcm {
    struct pcm *pcm;

    struct pollfd *pe;
    size_t pe_count; /* number of pollfd entries */

    signed short *buf;
    struct pcm_config *config;
    int rate;
};


struct alsa {
    struct alsa_pcm capture, playback;
};


static void alsa_error(const char *msg, struct pcm *pcm)
{
    fprintf(stderr, "ALSA %s: %s\n", msg, pcm_get_error(pcm));
}


static int xwax_pcm_open(struct alsa_pcm *alsa, unsigned int card, unsigned int device,
                    int rate, unsigned int mode, int period_size, int period_count)
{
    int r, dir;
    unsigned int p;
    size_t bytes;

    alsa->config->channels = DEVICE_CHANNELS;
    alsa->config->format = PCM_FORMAT_S16_LE;
    alsa->config->rate = rate;
    alsa->config->period_size = period_size;
    alsa->config->period_count = period_count;
    
    alsa->pcm = pcm_open(card, device, mode, alsa->config);
    if (pcm_is_ready(alsa->pcm) > 0) {
        alsa_error("open", alsa->pcm);
        return -1;
    }
        
    bytes = alsa->config->period_size * DEVICE_CHANNELS * sizeof(signed short);
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


static void xwax_pcm_close(struct alsa_pcm *alsa)
{
    if (pcm_close(alsa->pcm) < 0)
        abort();
    free(alsa->buf);
}


static ssize_t pcm_pollfds(struct alsa_pcm *alsa, struct pollfd *pe,
			   size_t z)
{
    int r;
    struct timespec tstamp;
    
    r = pcm_get_htimestamp(alsa->pcm, &alsa->pe_count,
                            &tstamp);
    if (r < 0) {
        alsa_error("pcm_get_htimestamp", alsa->pcm);
        return -1;
    }
    
    return alsa->pe_count;
}


static int pcm_revents(struct alsa_pcm *alsa) {
    int r;
    struct timespec tstamp;
    
    r = pcm_get_htimestamp(alsa->pcm, &alsa->pe_count,
                            &tstamp);
    if (r < 0) {
        alsa_error("pcm_get_htimestamp", alsa->pcm);
        return -1;
    }
    
    return 0;
}



/* Start the audio device capture and playback */

static void start(struct device *dv)
{
    struct alsa *alsa = (struct alsa*)dv->local;

    if (pcm_start(alsa->capture.pcm) < 0)
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

    device_collect(dv, alsa->playback.buf, alsa->playback.config->period_size);

    r = pcm_write(alsa->playback.pcm, alsa->playback.buf, 
                  alsa->playback.config->period_size);
    if (r < 0)
        return r;
        
    if (r < alsa->playback.config->period_size) {
        fprintf(stderr, "alsa: playback underrun %d/%ld.\n", r,
                alsa->playback.config->period_size);
    }

    return 0;
}


/* Pull audio from the device's buffer for capture, and pass it
 * through to the timecoder */

static int capture(struct device *dv)
{
    int r;
    struct alsa *alsa = (struct alsa*)dv->local;

    r = pcm_read(alsa->capture.pcm, alsa->capture.buf, 
                 alsa->capture.config->period_size);
    if (r < 0)
        return r;
    
    if (r < alsa->capture.config->period_size) {
        fprintf(stderr, "alsa: capture underrun %d/%ld.\n",
                r, alsa->capture.config->period_size);
    }

    device_submit(dv, alsa->capture.buf, r);

    return 0;
}


/* After poll() has returned, instruct a device to do all it can at
 * the present time. Return zero if success, otherwise -1 */

static int handle(struct device *dv)
{
    int r;
    struct alsa *alsa = (struct alsa*)dv->local;

    /* Check input buffer for timecode capture */
    
    r = pcm_revents(&alsa->capture);
    if (r < 0)
        return -1;
    
        r = capture(dv);
        
    if (r < 0) {
        if (r == PCM_STATE_XRUN) {
            fputs("ALSA: capture xrun.\n", stderr);

            r = pcm_start(alsa->capture.pcm);
            if (r < 0) {
                alsa_error("start", alsa->capture.pcm);
                return -1;
            }

        } else {
            alsa_error("capture", alsa->capture.pcm);
            return -1;
        }

    }
    
    /* Check the output buffer for playback */
    
    r = pcm_revents(&alsa->playback);
    if (r < 0)
        return -1;
    
        r = playback(dv);
        
    if (r < 0) {
        if (r == PCM_STATE_XRUN) {
            fputs("ALSA: playback xrun.\n", stderr);
            
            r = snd_pcm_prepare(alsa->playback.pcm);
            if (r < 0) {
                alsa_error("prepare", alsa->playback.pcm);
                return -1;
            }

            /* The device starts when data is written. POLLOUT
             * events are generated in prepared state. */

        } else {
            alsa_error("playback", alsa->playback.pcm);
            return -1;
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

    xwax_pcm_close(&alsa->capture);
    xwax_pcm_close(&alsa->playback);
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

int alsa_init(struct device *dv, unsigned int card, unsigned int device,
              int rate, int period_size, int period_count)
{
    struct alsa *alsa;

    alsa = malloc(sizeof *alsa);
    if (alsa == NULL) {
        perror("malloc");
        return -1;
    }

    if (xwax_pcm_open(&alsa->capture, card, device, PCM_IN,
                rate, period_size, period_count) < 0)
    {
        fputs("Failed to open device for capture.\n", stderr);
        goto fail;
    }
    
    if (xwax_pcm_open(&alsa->playback, card, device, PCM_OUT,
                rate, period_size, period_count) < 0)
    {
        fputs("Failed to open device for playback.\n", stderr);
        goto fail_capture;
    }

    device_init(dv, &alsa_ops);
    dv->local = alsa;

    return 0;

 fail_capture:
    xwax_pcm_close(&alsa->capture);
 fail:
    free(alsa);
    return -1;
}


/* ALSA caches information when devices are open. Provide a call
 * to clear these caches so that valgrind output is clean. */

void alsa_clear_config_cache(void)
{
    // Tinyalsa does not need to free config
}
