//
//   Copyright (C) 2005, 2006, 2007, 2008, 2009 Free Software Foundation, Inc.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
//

#ifdef HAVE_CONFIG_H
#include "gnashconfig.h"
#endif

#include "sound_handler.h"
#include "EmbedSound.h" // for use
#include "InputStream.h" // for use
#include "EmbedSoundInst.h" // for upcasting to InputStream
#include "log.h" // for use

#include <boost/cstdint.hpp> // For C99 int types
#include <vector> // for use

// Debug create_sound/delete_sound/playSound/stop_sound, loops
//#define GNASH_DEBUG_SOUNDS_MANAGEMENT

// Debug samples fetching
//#define GNASH_DEBUG_SAMPLES_FETCHING

namespace gnash {
namespace sound {

sound_handler::StreamBlockId
sound_handler::addSoundBlock(unsigned char* data,
        unsigned int data_bytes, unsigned int /*sample_count*/,
        int handleId)
{
    // @@ does a negative handle_id have any meaning ?
    //    should we change it to unsigned instead ?
    if (handleId < 0 || (unsigned int) handleId+1 > _sounds.size())
    {
        log_error("Invalid (%d) sound_handle passed to fill_stream_data, "
                  "doing nothing", handleId);
        delete [] data;
        return -1;
    }

    EmbedSound* sounddata = _sounds[handleId];
    if ( ! sounddata )
    {
        log_error("sound_handle passed to fill_stream_data (%d) "
                  "was deleted", handleId);
        return -1;
    }

    // Handling of the sound data
    size_t start_size = sounddata->size();
    sounddata->append(reinterpret_cast<boost::uint8_t*>(data), data_bytes);

#ifdef GNASH_DEBUG_SOUNDS_MANAGEMENT
    log_debug("fill_stream_data: sound %d, %d samples (%d bytes) appended at offset %d",
        handleId, data_bytes/2, data_bytes, start_size);
#endif

    return start_size;
}

void
sound_handler::delete_all_sounds()
{
    for (Sounds::iterator i = _sounds.begin(),
                          e = _sounds.end(); i != e; ++i)
    {
        EmbedSound* sdef = *i;

        // The sound may have been deleted already.
        if (!sdef) continue;

        stopEmbedSoundInstances(*sdef);
        assert(!sdef->numPlayingInstances());

        delete sdef; 
    }
    _sounds.clear();
}

void
sound_handler::delete_sound(int sound_handle)
{
    // Check if the sound exists
    if (sound_handle < 0 || static_cast<unsigned int>(sound_handle) >= _sounds.size())
    {
        log_error("Invalid (%d) sound_handle passed to delete_sound, "
                  "doing nothing", sound_handle);
        return;
    }

#ifdef GNASH_DEBUG_SOUNDS_MANAGEMENT
    log_debug ("deleting sound :%d", sound_handle);
#endif

    EmbedSound* def = _sounds[sound_handle];
    if ( ! def )
    {
        log_error("sound_handle passed to delete_sound (%d) "
                  "already deleted", sound_handle);
        return;
    }
    
    stopEmbedSoundInstances(*def);
    delete def;
    _sounds[sound_handle] = 0;

}

void   
sound_handler::stop_all_sounds()
{
    for (Sounds::iterator i = _sounds.begin(),
                          e = _sounds.end(); i != e; ++i)
    {
        EmbedSound* sounddata = *i;
        if ( ! sounddata ) continue; // could have been deleted already
        stopEmbedSoundInstances(*sounddata);
    }
}

int
sound_handler::get_volume(int soundHandle)
{
    int ret;
    // Check if the sound exists.
    if (soundHandle >= 0 && static_cast<unsigned int>(soundHandle) < _sounds.size())
    {
        ret = _sounds[soundHandle]->volume;
    } else {
        ret = 0; // Invalid handle
    }
    return ret;
}

void   
sound_handler::set_volume(int sound_handle, int volume)
{
    // Check if the sound exists.
    if (sound_handle < 0 || static_cast<unsigned int>(sound_handle) >= _sounds.size())
    {
        // Invalid handle.
    } else {

        // Set volume for this sound. Should this only apply to the active sounds?
        _sounds[sound_handle]->volume = volume;
    }


}

media::SoundInfo*
sound_handler::get_sound_info(int sound_handle)
{
    // Check if the sound exists.
    if (sound_handle >= 0 && static_cast<unsigned int>(sound_handle) < _sounds.size())
    {
        return _sounds[sound_handle]->soundinfo.get();
    } else {
        return NULL;
    }
}

void
sound_handler::stop_sound(int sound_handle)
{
    // Check if the sound exists.
    if (sound_handle < 0 || (unsigned int) sound_handle >= _sounds.size())
    {
        log_debug("stop_sound(%d): invalid sound id", sound_handle);
        // Invalid handle.
        return;
    }

    
    EmbedSound* sounddata = _sounds[sound_handle];
    if ( ! sounddata )
    {
        log_error("stop_sound(%d): sound was deleted", sound_handle);
        return;
    }

#ifdef GNASH_DEBUG_SOUNDS_MANAGEMENT
    log_debug("stop_sound %d called", sound_handle);
#endif

    stopEmbedSoundInstances(*sounddata);

}

void   
sound_handler::stopEmbedSoundInstances(EmbedSound& def)
{
    // Assert _mutex is locked ...

    typedef std::vector<InputStream*> InputStreamVect;
    InputStreamVect playing;
    def.getPlayingInstances(playing);

    // Now, for each playing InputStream, unplug it!
    // NOTE: could be optimized...
    for (InputStreamVect::iterator i=playing.begin(), e=playing.end();
            i!=e; ++i)
    {
#ifdef GNASH_DEBUG_SOUNDS_MANAGEMENT
        log_debug(" unplugging input stream %p from stopEmbedSoundInstances", *i);
#endif

        unplugInputStream(*i);
    }

    def.clearInstances();
}

void
sound_handler::unplugInputStream(InputStream* id)
{
    // WARNING: erasing would break any iteration in the set
    InputStreams::iterator it2=_inputStreams.find(id);
    if ( it2 == _inputStreams.end() )
    {
        log_error("SDL_sound_handler::unplugInputStream: "
                "Aux streamer %p not found. ",
                id);
        return; // we won't delete it, as it's likely deleted already
    }

    _inputStreams.erase(it2);

    // Increment number of sound stop request for the testing framework
    _soundsStopped++;

#ifdef GNASH_DEBUG_SOUNDS_MANAGEMENT
    log_debug("Unplugged InputStream %p", id);
#endif

    // Delete the InputStream (we own it..)
    delete id;
}

unsigned int
sound_handler::tell(int sound_handle)
{
    // Check if the sound exists.
    if (sound_handle < 0 || (unsigned int) sound_handle >= _sounds.size())
    {
        // Invalid handle.
        return 0;
    }

    EmbedSound* sounddata = _sounds[sound_handle];

    // If there is no active sounds, return 0
    if ( ! sounddata->isPlaying() ) return 0;

    // We use the first active sound of this.
    InputStream* asound = sounddata->firstPlayingInstance();

    // Return the playhead position in milliseconds
    unsigned int samplesPlayed = asound->samplesFetched();

    unsigned int ret = samplesPlayed / 44100 * 1000;
    ret += ((samplesPlayed % 44100) * 1000) / 44100;
    ret = ret / 2; // 2 channels 
    return ret;
}

unsigned int
sound_handler::get_duration(int sound_handle)
{
    // Check if the sound exists.
    if (sound_handle < 0 || (unsigned int) sound_handle >= _sounds.size())
    {
        // Invalid handle.
        return 0;
    }

    EmbedSound* sounddata = _sounds[sound_handle];

    boost::uint32_t sampleCount = sounddata->soundinfo->getSampleCount();
    boost::uint32_t sampleRate = sounddata->soundinfo->getSampleRate();

    // Return the sound duration in milliseconds
    if (sampleCount > 0 && sampleRate > 0) {
        // TODO: should we cache this in the EmbedSound object ?
        unsigned int ret = sampleCount / sampleRate * 1000;
        ret += ((sampleCount % sampleRate) * 1000) / sampleRate;
        //if (sounddata->soundinfo->isStereo()) ret = ret / 2;
        return ret;
    } else {
        return 0;
    }
}

int
sound_handler::create_sound(std::auto_ptr<SimpleBuffer> data,
                            std::auto_ptr<media::SoundInfo> sinfo)
{
    assert(sinfo.get());

    std::auto_ptr<EmbedSound> sounddata ( new EmbedSound(data, sinfo) );

    int sound_id = _sounds.size();

#ifdef GNASH_DEBUG_SOUNDS_MANAGEMENT
    log_debug("create_sound: sound %d, format %s %s %dHz, %d samples (%d bytes)",
        sound_id, sounddata->soundinfo->getFormat(),
        sounddata->soundinfo->isStereo() ? "stereo" : "mono",
        sounddata->soundinfo->getSampleRate(),
        sounddata->soundinfo->getSampleCount(),
        sounddata->size());
#endif

    // the vector takes ownership
    _sounds.push_back(sounddata.release());

    return sound_id;

}

void
sound_handler::playSound(int sound_handle, int loopCount, int offSecs,
                            StreamBlockId blockId,
                            const SoundEnvelopes* envelopes,
                            bool allowMultiples)
{
    // Check if the sound exists
    if (sound_handle < 0 || static_cast<unsigned int>(sound_handle) >= _sounds.size())
    {
        log_error("Invalid (%d) sound_handle passed to playSound, "
                  "doing nothing", sound_handle);
        return;
    }

    // parameter checking
    if (offSecs < 0)
    {
        log_error("Negative (%d) seconds offset passed to playSound, "
                  "taking as zero ", offSecs);
        offSecs = 0;
    }

    EmbedSound& sounddata = *(_sounds[sound_handle]);

#ifdef GNASH_DEBUG_SOUNDS_MANAGEMENT
    log_debug("playSound %d called, SoundInfo format is %s",
            sound_handle, sounddata.soundinfo->getFormat());
#endif

    // When this is called from a StreamSoundBlockTag,
    // we only start if this sound isn't already playing.
    if ( ! allowMultiples && sounddata.isPlaying() )
    {
#ifdef GNASH_DEBUG_SOUNDS_MANAGEMENT
        log_debug(" playSound: multiple instances not allowed, "
                  "and sound %d is already playing", sound_handle);
#endif
        // log_debug("Stream sound block play request, "
        //           "but an instance of the stream is "
        //           "already playing, so we do nothing");
        return;
    }

    // Make sure sound actually got some data
    if ( sounddata.empty() )
    {
        // @@ should this be a log_error ? or even an assert ?
        IF_VERBOSE_MALFORMED_SWF(
            log_swferror(_("Trying to play sound with size 0"));
        );
        return;
    }

    if (offSecs)
    {
        LOG_ONCE(log_unimpl("Time based sound start offset"));
    }

    // Make a "EmbedSoundInst" for this sound which is later placed
    // on the vector of instances of this sound being played
    //
    // @todo: plug the returned EmbedSoundInst to the set
    //        of InputStream channels !
    //
    std::auto_ptr<InputStream> sound ( sounddata.createInstance(

            // MediaHandler to use for decoding
            *_mediaHandler,

            // Sound block identifier
            blockId, 

            // Seconds offset
            // WARNING: this is wrong, offset is passed as seconds !!
            // (currently unused anyway)
            (sounddata.soundinfo->isStereo() ? offSecs : offSecs*2),

            // Volume envelopes to use for this instance
            envelopes,

            // Loop count
            loopCount

    ) );

    plugInputStream(sound);
}

/*public*/
void
sound_handler::playStream(int soundId, StreamBlockId blockId)
{
    playSound(soundId, 0, 0, blockId, 0, false);
}

/*public*/
void
sound_handler::startSound(int soundId, int loops, int secsOffset,
	               const SoundEnvelopes* env,
	               bool allowMultiple)
{
    playSound(soundId, loops, secsOffset, 0, env, allowMultiple);
}

void
sound_handler::plugInputStream(std::auto_ptr<InputStream> newStreamer)
{
#ifdef GNASH_DEBUG_SOUNDS_MANAGEMENT
    InputStream* newStream = newStreamer.get(); // for debugging
#endif

    if ( ! _inputStreams.insert(newStreamer.release()).second )
    {
        // this should never happen !
        log_error("_inputStreams container still has a pointer "
            "to deleted InputStream %p!", newStreamer.get());
        // FIXME: replace the old element with the new one !
        abort();
    }

#ifdef GNASH_DEBUG_SOUNDS_MANAGEMENT
    log_debug("Plugged InputStream %p", newStream);
#endif

    // Increment number of sound start request for the testing framework
    ++_soundsStarted;
}

void
sound_handler::unplugAllInputStreams()
{
    for (InputStreams::iterator it=_inputStreams.begin(),
                                itE=_inputStreams.end();
            it != itE; ++it)
    {
        delete *it;
    }
    _inputStreams.clear();
}

void
sound_handler::fetchSamples (boost::int16_t* to, unsigned int nSamples)
{
    if ( isPaused() ) return;

    float finalVolumeFact = getFinalVolume()/100.0;

    std::fill(to, to+nSamples, 0);

    // call NetStream or Sound audio callbacks
    if ( !_inputStreams.empty() )
    {
        // A buffer to fetch InputStream samples into
        boost::scoped_array<boost::int16_t> buf ( new boost::int16_t[nSamples] );

#ifdef GNASH_DEBUG_SAMPLES_FETCHING
        log_debug("Fetching %d samples for %d input streams", nSamples, _inputStreams.size());
#endif

        // Loop through the aux streamers sounds
        for (InputStreams::iterator it=_inputStreams.begin(),
                                    end=_inputStreams.end();
                                    it != end; ++it)
        {
            InputStream* is = *it;

            unsigned int wrote = is->fetchSamples(buf.get(), nSamples);
            if ( wrote < nSamples )
            {
                // fill what wasn't written
                std::fill(buf.get()+wrote, buf.get()+nSamples, 0);
            }

#ifdef GNASH_DEBUG_SAMPLES_FETCHING
            log_debug("  fetched %d/%d samples from input stream %p"
                    " (%d samples fetchehd in total)",
                    wrote, nSamples, is, is->samplesFetched());
#endif

            mix(to, buf.get(), nSamples, finalVolumeFact);
        }

        unplugCompletedInputStreams();
    }

    // Now, after having "consumed" all sounds, blank out
    // the buffer if muted..
    if ( is_muted() )
    {
        std::fill(to, to+nSamples, 0);
    }
}

/*private*/
void
sound_handler::unplugCompletedInputStreams()
{
    InputStreams::iterator it = _inputStreams.begin();
    InputStreams::iterator end = _inputStreams.end();

#ifdef GNASH_DEBUG_SOUNDS_MANAGEMENT
    log_debug("Scanning %d input streams for completion", _inputStreams.size());
#endif

    while (it != end)
    {
        InputStream* is = *it;

        // On EOF, detach
        if (is->eof())
        {
            // InputStream EOF, detach
            InputStreams::iterator it2=it;
            ++it2; // before we erase it
            InputStreams::size_type erased = _inputStreams.erase(is);
            if ( erased != 1 ) {
                log_error("Expected 1 InputStream element, found %d", erased);
                abort();
            }
            it = it2;

            //log_debug("fetchSamples: marking stopped InputStream %p (on EOF)", is);
#ifdef GNASH_DEBUG_SOUNDS_MANAGEMENT
            log_debug(" Input stream %p reached EOF, unplugging", is);
#endif

            // WARNING! deleting the InputStream here means
            //          a lot of things will happen from a
            //          separate thread. Instead, if we
            //          extend sound_handler interface to
            //          have an unplugCompletedInputStreams
            //          we may call it at heart-beating intervals
            //          and drop any threading paranoia!
            delete is;

            // Increment number of sound stop request for the testing framework
            _soundsStopped++;
        }
        else
        {
            ++it;
        }
    }
}

bool
sound_handler::hasInputStreams() const
{
    return !_inputStreams.empty();
}

void
sound_handler::mix(boost::int16_t* outSamples, boost::int16_t* inSamples,
                unsigned int nSamples, float /*volume*/)
{
    /// @todo implement a better mixer !
    // cheating, just copy input to output!
    std::copy(outSamples, outSamples+nSamples, inSamples);
}

bool
sound_handler::is_muted() const
{
    // TODO: lock a mutex ?
    return _muted;
}

void
sound_handler::mute()
{
    // TODO: lock a mutex ?
    _muted=true;
}

void
sound_handler::unmute()
{
    // TODO: lock a mutex ?
    _muted=false;
}

void
sound_handler::reset()
{
    sound_handler::delete_all_sounds();
    sound_handler::stop_all_sounds();
}

InputStream*
sound_handler::attach_aux_streamer(aux_streamer_ptr ptr, void* owner)
{
    assert(owner);
    assert(ptr);

    std::auto_ptr<InputStream> newStreamer ( new AuxStream(ptr, owner) );

    InputStream* ret = newStreamer.get();

    plugInputStream(newStreamer);

    return ret;
}

} // gnash.sound namespace 
} // namespace gnash
