#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/soundcard.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "NuppelVideoRecorder.h"

#define KEYFRAMEDISTEND   30
#define KEYFRAMEDISTSTART 5
#define BTTV_FIELDNR	_IOR('v' , BASE_VIDIOCPRIVATE+2, unsigned int)

NuppelVideoRecorder::NuppelVideoRecorder(void)
{
    sfilename = "output.nuv";
    encoding = false;
    fd = 0;
    lf = tf = 0;
    M1 = 0, M2 = 0, Q = 255;
    pid = pid2 = 0;
    inputchannel = 1;
    compression = 1;
    compressaudio = 1;
    ntsc = 1;
    rawmode = 0;
    usebttv = 1;
    w = 352;
    h = 240;

    mp3quality = 3;
    gf = NULL;
    rtjc = NULL;
    strm = NULL;   
    mp3buf = NULL;

    act_video_encode = 0;
    act_video_buffer = 0;
    act_audio_encode = 0;
    act_audio_buffer = 0;
    video_buffer_count = 0;
    audio_buffer_count = 0;
    video_buffer_size = 0;
    audio_buffer_size = 0;

    audiodevice = "/dev/dsp";
    videodevice = "/dev/video";

    childrenLive = false;

    recording = false;

    ringBuffer = NULL;
    weMadeBuffer = false;

    keyswritten = 0;
    keyframedist = KEYFRAMEDISTSTART;
}

NuppelVideoRecorder::~NuppelVideoRecorder(void)
{
    if (weMadeBuffer)
        delete ringBuffer;
    if (rtjc)
        delete rtjc;
    if (mp3buf)
        delete [] mp3buf;
    if (gf)
        lame_close(gf);  
    if (strm)
        delete [] strm;
    if (fd > 0)
        close(fd);
    
    while (videobuffer.size() > 0)
    {
        struct vidbuffertype *vb = videobuffer.back();
        delete [] vb->buffer;
        delete vb;
        videobuffer.pop_back();
    }
    while (audiobuffer.size() > 0)
    {
        struct audbuffertype *ab = audiobuffer.back();
        delete [] ab->buffer;
        delete ab;
        audiobuffer.pop_back();
    }
}

void NuppelVideoRecorder::Initialize(void)
{
    int videomegs;
    int audiomegs = 2;
    
    if (compressaudio)
    {
        gf = lame_init();
        lame_set_out_samplerate(gf, 44100);
	lame_set_bWriteVbrTag(gf, 0);
	lame_set_quality(gf, mp3quality);
	lame_set_compression_ratio(gf, 11);
	lame_init_params(gf);
    }

    video_buffer_size = w * h + ((w * h) >> 1);
    if (w >= 480 || h > 288) 
        videomegs = 14;
    else
        videomegs = 8;

    video_buffer_count = (videomegs * 1000 * 1000) / video_buffer_size;

    if (AudioInit() != 0)
    {
        fprintf(stderr, "Could not detect audio blocksize\n");
    }

    if (audio_buffer_size != 0)
        audio_buffer_count = (audiomegs * 1000 * 1000) / audio_buffer_size;
    else
        audio_buffer_count = 0;

    mp3buf_size = (int)(1.25 * 8192 + 7200);
    mp3buf = new char[mp3buf_size];

//  fprintf(stderr, "We are using %dx%ldB video buffers and %dx%ldB audio blocks\n", video_buffer_count, video_buffer_size, audio_buffer_count, audio_buffer_size);

    if (!ringBuffer)
    {
        ringBuffer = new RingBuffer(sfilename.c_str(), true, true);
	weMadeBuffer = true;
	livetv = false;
    }
    else
        livetv = true;

    InitBuffers();
}

int NuppelVideoRecorder::AudioInit(void)
{
    int afmt, afd;
    int frag, channels, rate, blocksize;

    if (-1 == (afd = open(audiodevice.c_str(), O_RDONLY)))
    {
        fprintf(stderr, "\n%s\n", "Cannot open DSP, exiting");
        return(1);
    }
  
    ioctl(afd, SNDCTL_DSP_RESET, 0);
   
    frag=(8<<16)|(10);//8 buffers, 1024 bytes each
    ioctl(afd, SNDCTL_DSP_SETFRAGMENT, &frag);

    afmt = AFMT_S16_LE;
    ioctl(afd, SNDCTL_DSP_SETFMT, &afmt);
    if (afmt != AFMT_S16_LE) 
    {
        fprintf(stderr, "\n%s\n", "Can't get 16 bit DSP, exiting");
        return(1);
    }

    channels = 2;
    ioctl(afd, SNDCTL_DSP_CHANNELS, &channels);

    /* sample rate */
    rate = 44100;
    ioctl(afd, SNDCTL_DSP_SPEED,    &rate);

    if (-1 == ioctl(afd, SNDCTL_DSP_GETBLKSIZE,  &blocksize)) 
    {
        fprintf(stderr, "\n%s\n", "Can't get DSP blocksize, exiting");
        return(1);
    }
    blocksize *= 4;

    close(afd);
  
    audio_buffer_size = blocksize;
    return(0); // everything is ok
}

void NuppelVideoRecorder::InitBuffers(void)
{
    for (int i = 0; i < video_buffer_count; i++)
    {
        vidbuffertype *vidbuf = new vidbuffertype;
        vidbuf->buffer = new unsigned char[video_buffer_size];
        vidbuf->sample = 0;
        vidbuf->freeToEncode = 0;
        vidbuf->freeToBuffer = 1;
       
        videobuffer.push_back(vidbuf);
    }

    for (int i = 0; i < audio_buffer_count; i++)
    {
        audbuffertype *audbuf = new audbuffertype;
        audbuf->buffer = new unsigned char[audio_buffer_size];
        audbuf->sample = 0;
        audbuf->freeToEncode = 0;
        audbuf->freeToBuffer = 1;
      
        audiobuffer.push_back(audbuf);
    }
}

void NuppelVideoRecorder::StartRecording(void)
{
    if (lzo_init() != LZO_E_OK)
    {
        fprintf(stderr, "lzo_init() failed\n");
        return;
    }

    strm = new signed char[w * h + (w * h) / 2 + 10];

    if (CreateNuppelFile() != 0)
    {
        fprintf(stderr, "Cannot open %s for writing\n", sfilename.c_str());
        return;
    }

    int i;
    rtjc = new RTjpeg();
    i = RTJ_YUV420;
    rtjc->SetFormat(&i);
    rtjc->SetSize(&w, &h);
    rtjc->SetQuality(&Q);
    i = 1;
    rtjc->SetIntra(&i, &M1, &M2);

    if (childrenLive)
        return;

    if (SpawnChildren() < 0)
        return;

    // save the start time
    gettimeofday(&stm, &tzone);

    if (getuid() == 0)
        nice(-10);

    fd = open(videodevice.c_str(), O_RDWR|O_CREAT);
    if (fd <= 0)
    {
        perror("open video:");
        KillChildren();
        return;
    }

    struct video_mmap mm;
    struct video_mbuf vm;
    struct video_channel vchan;
    struct video_audio va;
    struct video_tuner vt;

    if (ioctl(fd, VIDIOCGMBUF, &vm) < 0)
    {
        perror("VIDOCGMBUF:");
        KillChildren();
        return;
    }

    if (vm.frames < 2)
    {
        fprintf(stderr, "need a minimum of 2 capture buffers\n");
        KillChildren();
        return;
    }

    unsigned char *buf;
    int frame;

    buf = (unsigned char *)mmap(0, vm.size, PROT_READ|PROT_WRITE, MAP_SHARED, 
		                fd, 0);
    if (buf <= 0)
    {
        perror("mmap");
        KillChildren();
        return;
    }

    int channel = 0;
    int volume = -1;

    vchan.channel = channel;
    if (ioctl(fd, VIDIOCGCHAN, &vchan) < 0) 
        perror("VIDIOCGCHAN");

    // choose the right input
    if (ioctl(fd, VIDIOCSCHAN, &vchan) < 0) 
        perror("VIDIOCSCHAN");
  
    // if channel has a audio then activate it
    if ((vchan.flags & VIDEO_VC_AUDIO)==VIDEO_VC_AUDIO) {
        if (!quiet) 
            fprintf(stderr, "%s\n", "unmuting tv-audio");
        // audio hack, to enable audio from tvcard, in case we use a tuner
        va.audio = 0; // use audio channel 0
        if (ioctl(fd, VIDIOCGAUDIO, &va)<0) 
            perror("VIDIOCGAUDIO"); 
        origaudio = va;
        if (!quiet) 
            fprintf(stderr, "audio volume was '%d'\n", va.volume);
        va.audio = 0;
        va.flags &= ~VIDEO_AUDIO_MUTE; // now this really has to work

        if ((volume==-1 && va.volume<32768) || volume!=-1) 
	{
            if (volume==-1) 
                va.volume = 32768;            // no more silence 8-)
            else 
                va.volume = volume;
            if (!quiet) 
                fprintf(stderr, "audio volume set to '%d'\n", va.volume);
        }
        if (ioctl(fd, VIDIOCSAUDIO, &va)<0) 
            perror("VIDIOCSAUDIO");
    } 
    else 
    {
        if (!quiet) 
            fprintf(stderr, "channel '%d' has no tuner (composite)\n", channel);
    }

    vt.tuner = 0;
    if (ioctl(fd, VIDIOCGTUNER, &vt) < 0)
        perror("VIDIOCGTUNER");
    if (ntsc) 
    { 
        vt.flags |= VIDEO_TUNER_NTSC;  
        vt.mode |= VIDEO_MODE_NTSC;
    }  
    else 
    { 
        vt.flags |= VIDEO_TUNER_PAL;   
        vt.mode |= VIDEO_MODE_PAL;
    }     

    vt.tuner = 0;
    if (ioctl(fd, VIDIOCSTUNER, &vt)<0) 
        perror("VIDIOCSTUNER");
        
    // make sure we use the right input
    if (ioctl(fd, VIDIOCSCHAN, &vchan)<0) 
        perror("VIDIOCSCHAN");
         
    mm.height = h;
    mm.width  = w;
    mm.format = VIDEO_PALETTE_YUV420P    ; /* YCrCb422 */
  
    mm.frame  = 0;
    if (ioctl(fd, VIDIOCMCAPTURE, &mm)<0) 
        perror("VIDIOCMCAPTUREi0");
    mm.frame  = 1;
    if (ioctl(fd, VIDIOCMCAPTURE, &mm)<0) 
        perror("VIDIOCMCAPTUREi1");

    encoding = true;

    while (encoding) {
        frame = 0;
        mm.frame = 0;
        if (ioctl(fd, VIDIOCSYNC, &frame)<0) 
            perror("VIDIOCSYNC0");
        else 
        {
            if (ioctl(fd, VIDIOCMCAPTURE, &mm)<0) 
                perror("VIDIOCMCAPTURE0");
            BufferIt(buf+vm.offsets[0]);
        }
        frame = 1;
        mm.frame = 1;
        if (ioctl(fd, VIDIOCSYNC, &frame)<0) 
            perror("VIDIOCSYNC1");
        else 
        {
            if (ioctl(fd, VIDIOCMCAPTURE, &mm)<0) 
                perror("VIDIOCMCAPTURE1");
            BufferIt(buf+vm.offsets[1]);
        }
    }

    KillChildren();
}

int NuppelVideoRecorder::SpawnChildren(void)
{
    int result;

    childrenLive = true;
    
    result = pthread_create(&write_tid, NULL, 
		            NuppelVideoRecorder::WriteThread, this);

    if (result)
    {
        fprintf(stderr, "Couldn't spawn writer thread\n");
	return -1;
    }

    result = pthread_create(&audio_tid, NULL,
                            NuppelVideoRecorder::AudioThread, this);

    if (result)
    {
        fprintf(stderr, "Couldn't spawn audio thread\n");
	return -1;
    }

    return 0;
}

void NuppelVideoRecorder::KillChildren(void)
{
    childrenLive = false;
    
    if (ioctl(fd, VIDIOCSAUDIO, &origaudio)<0) 
        perror("VIDIOCSAUDIO");

    pthread_join(write_tid, NULL);
    pthread_join(audio_tid, NULL);
}

void NuppelVideoRecorder::BufferIt(unsigned char *buf)
{
    int act;
    long tcres;
    static int oldtc = 0;
    int fn;

    act = act_video_buffer;
   
    if (!videobuffer[act]->freeToBuffer) {
        // we have to skip the current frame :(
        return;
    }

    gettimeofday(&now, &tzone);
   
    tcres = (now.tv_sec-stm.tv_sec)*1000 + now.tv_usec/1000 - stm.tv_usec/1000;

    if (usebttv) {
        if (ioctl(fd, BTTV_FIELDNR, &tf)) 
        {
            perror("BTTV_FIELDNR");
            usebttv = 0;
            fprintf(stderr, "\nbttv_fieldnr not supported by bttv-driver"
                            "\nuse insmod/modprobe bttv card=YOURCARD field_nr=1 to activate f.n."
                            "\nfalling back to timecode routine to determine lost frames\n");
         }
         if (tf==0) 
         {
             usebttv = 0;
             fprintf(stderr, "\nbttv_fieldnr not supported by bttv-driver"
                             "\nuse insmod/modprobe bttv card=YOURCARD field_nr=1 to activate f.n."
                             "\nfalling back to timecode routine to determine lost frames\n");
         }
    }

    // here is the non preferable timecode - drop algorithm - fallback
    if (!usebttv) 
    {
        if (tf==0)
            tf = 2;
        else 
        {
            fn = tcres - oldtc;

     // the difference should be less than 1,5*timeperframe or we have 
     // missed at least one frame, this code might be inaccurate!
     
            if (ntsc)
                fn = fn/33;
            else 
                fn = fn/40;
            if (fn==0) 
                fn=1;
            tf += 2*fn; // two fields
        }
    }

    oldtc = tcres;

    if (!videobuffer[act]->freeToBuffer) 
        return; // we can't buffer the current frame

    videobuffer[act]->sample = tf;
    videobuffer[act]->timecode = tcres;

    memcpy(videobuffer[act]->buffer, buf, video_buffer_size);

    videobuffer[act]->freeToBuffer = 0;
    act_video_buffer++;
    if (act_video_buffer >= video_buffer_count) 
        act_video_buffer = 0; // cycle to begin of buffer
    videobuffer[act]->freeToEncode = 1; // set last to prevent race
    return;
}

int NuppelVideoRecorder::CreateNuppelFile(void)
{
    struct rtfileheader fileheader;
    struct rtframeheader frameheader;
    char realfname[255];
    static unsigned long int tbls[128];
    static const char finfo[12] = "NuppelVideo";
    static const char vers[5]   = "0.05";

    snprintf(realfname, 250, "%s", sfilename.c_str());

    framesWritten = 0;

    if (!ringBuffer)
    {
        ringBuffer = new RingBuffer(sfilename.c_str(), true, true);
        weMadeBuffer = true;
	livetv = false;
    }
    else
        livetv = true;

    if (!ringBuffer->IsOpen()) 
        return(-1);

    memcpy(fileheader.finfo, finfo, sizeof(fileheader.finfo));
    memcpy(fileheader.version, vers, sizeof(fileheader.version));
    fileheader.width  = w;
    fileheader.height = h;
    fileheader.desiredwidth  = 0;
    fileheader.desiredheight = 0;
    fileheader.pimode = 'P';
    fileheader.aspect = 1.0;
    if (ntsc) 
        fileheader.fps = 29.97;
    else  
        fileheader.fps = 25.0;
    fileheader.videoblocks = -1;
    fileheader.audioblocks = -1;
    fileheader.textsblocks = 0;
    fileheader.keyframedist = KEYFRAMEDISTEND;
    ringBuffer->Write(&fileheader, FILEHEADERSIZE);
    
    frameheader.frametype = 'D'; // compressor data
    frameheader.comptype  = 'R'; // compressor data for RTjpeg
    frameheader.packetlength = sizeof(tbls);

    // compression configuration header
    ringBuffer->Write(&frameheader, FRAMEHEADERSIZE);
    
    // compression configuration data
    ringBuffer->Write(tbls, sizeof(tbls));

    lf = 0; // that resets framenumber so that seeking in the
            // continues parts works too

    return(0);
}

void *NuppelVideoRecorder::WriteThread(void *param)
{
    NuppelVideoRecorder *nvr = (NuppelVideoRecorder *)param;

    nvr->doWriteThread();

    return NULL;
}

void *NuppelVideoRecorder::AudioThread(void *param)
{
    NuppelVideoRecorder *nvr = (NuppelVideoRecorder *)param;

    nvr->doAudioThread();

    return NULL;
}

void NuppelVideoRecorder::doAudioThread(void)
{
    int afmt,trigger;
    int afd, act, lastread;
    int frag, channels, rate, blocksize;
    unsigned char *buffer;
    long tcres;

    long long act_audio_sample=0;

    if (-1 == (afd = open(audiodevice.c_str(), O_RDONLY))) 
    {
        fprintf(stderr, "\n%s\n", "Cannot open DSP, exiting");
        return;
    }

    ioctl(afd, SNDCTL_DSP_RESET, 0);

    frag = (8 << 16) | (10); //8 buffers, 1024 bytes each
    ioctl(afd, SNDCTL_DSP_SETFRAGMENT, &frag);

    afmt = AFMT_S16_LE;
    ioctl(afd, SNDCTL_DSP_SETFMT, &afmt);
    if (afmt != AFMT_S16_LE) 
    {
        fprintf(stderr, "\n%s\n", "Can't get 16 bit DSP, exiting");
        close(afd);
        return;
    }

    channels = 2;
    ioctl(afd, SNDCTL_DSP_CHANNELS, &channels);

    /* sample rate */
    rate = 44100;
    ioctl(afd, SNDCTL_DSP_SPEED, &rate);

    if (-1 == ioctl(afd, SNDCTL_DSP_GETBLKSIZE,  &blocksize)) 
    {
        fprintf(stderr, "\n%s\n", "Can't get DSP blocksize, exiting");
        close(afd);
        return;
    }

    blocksize *= 4;  // allways read 4*blocksize

    if (blocksize != audio_buffer_size) 
    {
        fprintf(stderr, "\nwarning: audio blocksize = '%d'"
                        "audio_buffer_size='%ld'\n",
                        blocksize, audio_buffer_size);
    }

    buffer = new unsigned char[audio_buffer_size];

    /* trigger record */
    trigger = ~PCM_ENABLE_INPUT;
    ioctl(afd,SNDCTL_DSP_SETTRIGGER,&trigger);

    trigger = PCM_ENABLE_INPUT;
    ioctl(afd,SNDCTL_DSP_SETTRIGGER,&trigger);

    while (childrenLive) {
        gettimeofday(&anow, &tzone);

        if (audio_buffer_size != (lastread = read(afd, buffer,
                                                  audio_buffer_size))) 
        {
            fprintf(stderr, "only read %d from %ld bytes from '%s'\n", 
                    lastread, audio_buffer_size, audiodevice.c_str());
            perror("read audio");
        }

        act = act_audio_buffer;

        if (!audiobuffer[act]->freeToBuffer) 
        {
            fprintf(stderr, "ran out of free AUDIO buffers :-(\n");
            act_audio_sample++;
            continue;
        }

        tcres = (anow.tv_sec-stm.tv_sec)*1000 + now.tv_usec/1000 - 
                stm.tv_usec/1000;
        audiobuffer[act]->sample = act_audio_sample;
        audiobuffer[act]->timecode = tcres;

        memcpy(audiobuffer[act]->buffer, buffer, audio_buffer_size);

        audiobuffer[act]->freeToBuffer = 0;
        act_audio_buffer++;
        if (act_audio_buffer >= audio_buffer_count) 
            act_audio_buffer = 0; 
        audiobuffer[act]->freeToEncode = 1; 

        act_audio_sample++; 
    }

    delete [] buffer;
}

void NuppelVideoRecorder::doWriteThread(void)
{
    int act;
    int videofirst;

    while (childrenLive)
    {
        act = act_video_encode;
        if (!videobuffer[act]->freeToEncode && 
            !audiobuffer[act_audio_encode]->freeToEncode)
        {
            usleep(5);
            continue;
        }

        if (videobuffer[act]->freeToEncode)
        {
            if (audiobuffer[act_audio_encode]->freeToEncode) 
            {
                videofirst = (videobuffer[act]->timecode <=
                              audiobuffer[act_audio_encode]->timecode);
            }
            else
                videofirst = 1;
        }
        else
            videofirst = 0;

        if (videofirst)
        {
            if (videobuffer[act]->freeToEncode)
            {
                WriteVideo(videobuffer[act]->buffer, videobuffer[act]->sample,
                           videobuffer[act]->timecode);
                videobuffer[act]->sample = 0;
                act_video_encode++;
                if (act_video_encode >= video_buffer_count)
                    act_video_encode = 0;
                videobuffer[act]->freeToEncode = 0;
                videobuffer[act]->freeToBuffer = 1;
            }
        }
        else
        {
            if (audiobuffer[act_audio_encode]->freeToEncode)
            {
                WriteAudio(audiobuffer[act_audio_encode]->buffer,
                           audiobuffer[act_audio_encode]->sample,
                           audiobuffer[act_audio_encode]->timecode);
                audiobuffer[act_audio_encode]->sample = 0;
                audiobuffer[act_audio_encode]->freeToEncode = 0;
                audiobuffer[act_audio_encode]->freeToBuffer = 1; 
                act_audio_encode++;
                if (act_audio_encode >= audio_buffer_count) 
                    act_audio_encode = 0; 
            }
        }
    }
}

void NuppelVideoRecorder::WriteVideo(unsigned char *buf, int fnum, int timecode)
{
    int tmp, r = 0, out_len = OUT_LEN;
    struct rtframeheader frameheader;
    int xaa, freecount = 0, compressthis;
    static int startnum = 0;
    static int frameofgop = 0;
    static int lasttimecode = 0;
    int raw = 0;
    int timeperframe = 40;
    uint8_t *planes[3];

    planes[0] = buf;
    planes[1] = planes[0] + w*h;
    planes[2] = planes[1] + (w*h)/4;
    compressthis = compression;

    if (lf == 0) 
    { // this will be triggered every new file
        lf = fnum-2;
        startnum = fnum;
    }

    // count free buffers -- FIXME this can be done with less CPU time!!
    for (xaa = 0; xaa < video_buffer_count; xaa++) 
    {
        if (videobuffer[xaa]->freeToBuffer) 
            freecount++;
    }

    if (freecount < ((2 * video_buffer_count) / 3)) 
        compressthis = 0; // speed up the encode process

    if (freecount < 5 || rawmode) 
        raw = 1; // speed up the encode process

    // see if it's time for a seeker header, sync information and a keyframe
    frameheader.keyframe  = frameofgop;             // no keyframe defaulted

    if (((fnum-startnum)>>1) % keyframedist == 0) {
        frameheader.keyframe=0;
        frameofgop=0;
        ringBuffer->Write("RTjjjjjjjjjjjjjjjjjjjjjjjj", FRAMEHEADERSIZE);
        frameheader.frametype    = 'S';           // sync frame
        frameheader.comptype     = 'V';           // video sync information
        frameheader.filters      = 0;             // no filters applied
        frameheader.packetlength = 0;             // no data packet
        frameheader.timecode     = (fnum-startnum)>>1;  
        // write video sync info
        ringBuffer->Write(&frameheader, FRAMEHEADERSIZE);
        frameheader.frametype    = 'S';           // sync frame
        frameheader.comptype     = 'A';           // video sync information
        frameheader.filters      = 0;             // no filters applied
        frameheader.packetlength = 0;             // no data packet
        frameheader.timecode     = effectivedsp;  // effective dsp frequency
        // write audio sync info
        ringBuffer->Write(&frameheader, FRAMEHEADERSIZE);

	if (keyswritten < 7)
	{
            keyswritten++;
	    if (keyswritten > 6)
                keyframedist = KEYFRAMEDISTEND;
	}    
    }

    if (!raw) 
        tmp = rtjc->Compress(strm, planes);
    else 
        tmp = video_buffer_size;

    // here is lzo compression afterwards
    if (compressthis) {
        if (raw) 
            r = lzo1x_1_compress((unsigned char*)buf, video_buffer_size, out,
                                (lzo_uint *)&out_len, wrkmem);
        else
            r = lzo1x_1_compress((unsigned char *)strm, tmp, out,
                                 (lzo_uint *)&out_len, wrkmem);
        if (r != LZO_E_OK) 
        {
            fprintf(stderr,"%s\n","lzo compression failed");
            return;
        }
    }

    frameheader.frametype = 'V'; // video frame
    frameheader.timecode  = timecode;
    frameheader.filters   = 0;             // no filters applied

    dropped = (((fnum-lf)>>1) - 1); // should be += 0 ;-)

    if (dropped>0) 
    {
        timeperframe = (timecode - lasttimecode)/(dropped + 1); 
        if (timeperframe < 0) 
        {
            if (ntsc)
                timeperframe = 1000/30;
            else
                timeperframe = 1000/25;
       
        }
        frameheader.timecode = lasttimecode + timeperframe;
        lasttimecode = frameheader.timecode;
    }

    // compr ends here
    if (compressthis == 0 || (tmp < out_len)) 
    {
        if (!raw) 
        {
            frameheader.comptype  = '1'; // video compression: RTjpeg only
            frameheader.packetlength = tmp;
            ringBuffer->Write(&frameheader, FRAMEHEADERSIZE);
            ringBuffer->Write(strm, tmp);
        } 
        else 
        {
            frameheader.comptype  = '0'; // raw YUV420
            frameheader.packetlength = video_buffer_size;
            ringBuffer->Write(&frameheader, FRAMEHEADERSIZE);
            ringBuffer->Write(buf, video_buffer_size); // we write buf directly
        }
    } 
    else 
    {
        if (!raw) 
            frameheader.comptype  = '2'; // video compression: RTjpeg with lzo
        else
            frameheader.comptype  = '3'; // raw YUV420 with lzo
        frameheader.packetlength = out_len;
        ringBuffer->Write(&frameheader, FRAMEHEADERSIZE);
        ringBuffer->Write(out, out_len);
    }

    frameofgop++;
    framesWritten++;
    
    // if we have lost frames we insert "copied" frames until we have the
    // exact count because of that we should have no problems with audio 
    // sync, as long as we don't loose audio samples :-/
  
    while (dropped > 0) 
    {
	    printf("dropped frames\n");
        frameheader.timecode = lasttimecode + timeperframe;
        lasttimecode = frameheader.timecode;
        frameheader.keyframe  = frameofgop;             // no keyframe defaulted
        frameheader.packetlength =  0;   // no additional data needed
        frameheader.frametype    = 'V';  // last frame (or nullframe if first)
        frameheader.comptype    = 'L';
        ringBuffer->Write(&frameheader, FRAMEHEADERSIZE);
        // we don't calculate sizes for lost frames for compression computation
        dropped--;
        frameofgop++;
    }

    // now we reset the last frame number so that we can find out
    // how many frames we didn't get next time
    lf = fnum;
    lasttimecode = timecode;
}

void NuppelVideoRecorder::WriteAudio(unsigned char *buf, int fnum, int timecode)
{
    struct rtframeheader frameheader;
    static int last_block   = 0;
    static int audio_behind = 0;
    static int firsttc      = -1;
    double mt;
    double eff;
    double abytes;

    if (last_block != 0) 
    {
        if (fnum != (last_block+1)) 
        {
            audio_behind = fnum - (last_block+1);
        }
    }

    frameheader.frametype = 'A'; // audio frame

    if (compressaudio) 
    {
        frameheader.comptype  = '3'; // audio is compressed
    }
    else 
    {
        frameheader.comptype  = '0'; // audio is uncompressed
        frameheader.packetlength = audio_buffer_size;
    }
    frameheader.timecode = timecode;

    if (firsttc==-1) 
    {
        firsttc = timecode;
        //fprintf(stderr, "first timecode=%d\n", firsttc);
    } 
    else 
    {
        timecode -= firsttc; // this is to avoid the lack between the beginning
                             // of recording and the first timestamp, maybe we
                             // can calculate the audio-video +-lack at the 
                            // beginning too
        abytes = (double)audiobytes; // - (double)audio_buffer_size; 
                                     // wrong guess ;-)
        // need seconds instead of msec's
        //mt = (double)timecode/1000.0;
        mt = (double)timecode;
        if (mt > 0.0) 
        {
            //eff = (abytes/4.0)/mt;
            //effectivedsp=(int)(100.0*eff);
            eff = (abytes/mt)*((double)100000.0/(double)4.0);
            effectivedsp=(int)eff;
        }
    }

    if (compressaudio) 
    {
        char mp3gapless[7200];
        int compressedsize = 0;
        int gaplesssize = 0;
        int lameret = 0;

        lameret = lame_encode_buffer_interleaved(gf, (short int *)buf,
                                                 audio_buffer_size / 4,
                                                 (unsigned char *)mp3buf,
                                                 mp3buf_size);
        compressedsize = lameret;

        lameret = lame_encode_flush_nogap(gf, (unsigned char *)mp3gapless, 
                                          7200);
        gaplesssize = lameret;

        frameheader.packetlength = compressedsize + gaplesssize;

        ringBuffer->Write(&frameheader, FRAMEHEADERSIZE);
        ringBuffer->Write(mp3buf, compressedsize);
        ringBuffer->Write(mp3gapless, gaplesssize);

        audiobytes += audio_buffer_size;
    } 
    else 
    {
        ringBuffer->Write(&frameheader, FRAMEHEADERSIZE);
        ringBuffer->Write(buf, audio_buffer_size);
        audiobytes += audio_buffer_size; // only audio no header!!
    }

    // this will probably never happen and if there would be a 
    // 'uncountable' video frame drop -> material==worthless
    if (audio_behind > 0) 
    {
        fprintf(stderr, "audio behind!!!!!\n\n\n");
        frameheader.frametype = 'A'; // audio frame
        frameheader.comptype  = 'N'; // output a nullframe with
        frameheader.packetlength = 0;
        ringBuffer->Write(&frameheader, FRAMEHEADERSIZE);
        audiobytes += audio_buffer_size;
        audio_behind --;
    }

    last_block = fnum;
}

