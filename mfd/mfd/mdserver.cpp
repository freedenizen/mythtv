/*
	mdserver.cpp

	(c) 2003 Thor Sigvaldason and Isaac Richards
	Part of the mythTV project
	
	Implementation of a threaded object that opens a server port and
	explains the state of the metadata to any client that asks. It _also_
	holds all the metadatacontainers, so it is _the_ place to ask about what
	content is available

*/

#include <iostream>
using namespace std;

#include "settings.h"

#include "mdserver.h"
#include "../mfdlib/mfd_events.h"

MetadataServer::MetadataServer(MFD* owner, int port)
               :MFDServicePlugin(owner, -1, port, "metadata server")
{
    metadata_containers = new QPtrList<MetadataContainer>;
    metadata_containers->setAutoDelete(true);
    local_audio_metadata_containers = new QPtrList<MetadataContainer>;
    local_audio_metadata_containers->setAutoDelete(false);
    metadata_audio_generation = 4;  //  don't ask
    container_identifier = 0;
    
    local_audio_metadata_count = 0;
    local_audio_playlist_count = 0;
    metadata_container_count = 0;
}

void MetadataServer::run()
{
    //
    //  Advertise ourselves to the world (via zeroconfig) as an mdcap (Myth
    //  Digital Content Access Protocol) service
    //

    QString local_hostname = mfdContext->getHostName();

    ServiceEvent *se = new ServiceEvent(QString("services add mdcap %1 Myth Metadata Services on %2")
                                       .arg(port_number)
                                       .arg(local_hostname));
    QApplication::postEvent(parent, se);


    //
    //  Init our sockets (note, these are low-level Socket Devices, which
    //  are thread safe)
    //
    
    if( !initServerSocket())
    {
        fatal("metadata server could not initialize its core server socket");
        return;
    }
    
    while(keep_going)
    {
        //
        //  Update the status of our sockets.
        //
        
        updateSockets();
        waitForSomethingToHappen();
    }
}

void MetadataServer::doSomething(const QStringList &tokens, int socket_identifier)
{
    bool ok = false;

    if(tokens.count() < 1)
    {
        ok = false;
    }
    else if(tokens[0] == "check" && tokens.count() == 1)
    {
        ok = true;
        //checkMetadata();
    }

    if(!ok)
    {
        warning(QString("did not understand these tokens: %1").arg(tokens.join(" ")));
        huh(tokens, socket_identifier);
    }
}

void MetadataServer::lockMetadata()
{
    metadata_mutex.lock();
}

void MetadataServer::unlockMetadata()
{
    metadata_mutex.unlock();
}

uint MetadataServer::getMetadataAudioGeneration()
{
    uint return_value;
    metadata_audio_generation_mutex.lock();
        return_value = metadata_audio_generation;
    metadata_audio_generation_mutex.unlock();
    return return_value;
}

uint MetadataServer::getMetadataContainerCount()
{
    uint return_value = 0;
    metadata_container_count_mutex.lock();
        return_value = metadata_container_count;
    metadata_container_count_mutex.unlock();
    return return_value;
}

uint MetadataServer::getAllLocalAudioMetadataCount()
{
    uint return_value = 0;
    local_audio_metadata_count_mutex.lock();
        if(local_audio_metadata_count > -1)
        {
            return_value = (uint) local_audio_metadata_count;
        }
    local_audio_metadata_count_mutex.unlock();
    return return_value;
}

uint MetadataServer::getAllLocalAudioPlaylistCount()
{
    uint return_value = 0;
    local_audio_playlist_count_mutex.lock();
        if(local_audio_playlist_count > -1)
        {
            return_value = (uint) local_audio_playlist_count;
        }
    local_audio_playlist_count_mutex.unlock();
    return return_value;
}

Metadata* MetadataServer::getMetadataByUniversalId(uint universal_id)
{
    //
    //  Metadata should be locked _before_ calling this method
    //
    
    if(metadata_mutex.tryLock())
    {
        metadata_mutex.unlock();
        warning("getMetadataByUniversalId() called without "
                "metadata_mutex being locked");
    }

    if(universal_id <= METADATA_UNIVERSAL_ID_DIVIDER)
    {
        warning("something asked for metadata with "
                "universal id that is too small");
        return NULL;
    }
    
    int collection_id = universal_id / METADATA_UNIVERSAL_ID_DIVIDER;
    int item_id = universal_id % METADATA_UNIVERSAL_ID_DIVIDER;
    
    //
    //  Find the collection
    //
    
    Metadata *return_value = NULL;
    
    MetadataContainer * a_container;
    for (
            a_container = metadata_containers->first(); 
            a_container; 
            a_container = metadata_containers->next()
        )
    {
        if(a_container->getIdentifier() == collection_id)
        {
            return_value = a_container->getMetadata(item_id);
            break; 
        }
    }

    
    return return_value;
}

Playlist* MetadataServer::getPlaylistByUniversalId(uint universal_id)
{

    //
    //  Metadata should be locked _before_ calling this method
    //
    
    if(metadata_mutex.tryLock())
    {
        metadata_mutex.unlock();
        warning("getPlaylistByUniversalId() called without "
                "metadata_mutex being locked");
    }


    if(universal_id <= METADATA_UNIVERSAL_ID_DIVIDER)
    {
        warning("something asked for playlist with "
                "universal id that is too small");
        return NULL;
    }
    

    int collection_id = universal_id / METADATA_UNIVERSAL_ID_DIVIDER;
    int playlist_id = universal_id % METADATA_UNIVERSAL_ID_DIVIDER;
    
    //
    //  Find the collection
    //
    
    Playlist *return_value = NULL;
    
    MetadataContainer * a_container;
    for (
            a_container = metadata_containers->first(); 
            a_container; 
            a_container = metadata_containers->next()
        )
    {
        if(a_container->getIdentifier() == collection_id)
        {
            return_value = a_container->getPlaylist(playlist_id);
            break; 
        }
    }
    return return_value;
}

MetadataContainer* MetadataServer::createContainer(
                                                    MetadataCollectionContentType content_type,
                                                    MetadataCollectionLocationType location_type
                                                  )
{
    MetadataContainer *return_value;
    lockMetadata();
        return_value = new MetadataContainer(parent, bumpContainerId(), content_type, location_type);
        metadata_containers->append(return_value);
        
        //
        //  The metadata_containers pointer list (above) owns this object,
        //  and will delete them if told to or when it exits. The following
        //  container(s) are just shortcuts that make it easier to find
        //  stuff.
        //
        
        //
        //  NB: at some point, we will add mutex's around these ptr list
        //  containers, so, for example, video metadata can get accessed
        //  independently of audio metadata ...
        //

        if(content_type == MCCT_audio && location_type == MCLT_host)
        {
            local_audio_metadata_containers->append(return_value);
        }
    metadata_container_count_mutex.lock();
        ++metadata_container_count;
    metadata_container_count_mutex.unlock();
        
    unlockMetadata();
    
    return return_value;
}                                                                                                                                                        

int MetadataServer::bumpContainerId()
{
    int return_value;

    container_identifier_mutex.lock();
        ++container_identifier;
        return_value = container_identifier;
    container_identifier_mutex.unlock();
    
    return return_value;
}

void MetadataServer::doAtomicDataSwap(
                                        MetadataContainer *which_one,
                                        QIntDict<Metadata>* new_metadata,
                                        QValueList<int> metadata_additions,
                                        QValueList<int> metadata_deletions,
                                        QIntDict<Playlist>* new_playlists,
                                        QValueList<int> playlist_additions,
                                        QValueList<int> playlist_deletions
                                     )
{
    //
    //  Lock the metadata, find the right container, and swap out its data. 
    //  The idea is that a plugin can take as long as it wants to build a
    //  new metadata collection, but this call is fairly quick and all
    //  inside a mutex lock. Thus the name.
    //

    lockMetadata();

        MetadataContainer *target = NULL;
        MetadataContainer *a_container;
        for (
                a_container = metadata_containers->first(); 
                a_container; 
                a_container = metadata_containers->next()
            )
        {
            if(a_container == which_one)
            {
                target = a_container;
                break; 
            }
        }
        
        if(target)
        {
            local_audio_metadata_count_mutex.lock();
                local_audio_playlist_count_mutex.lock();
            
                    if(target->isLocal() && target->isAudio())
                    {
                        local_audio_metadata_count = 
                                local_audio_metadata_count 
                                - target->getMetadataCount();
                        local_audio_playlist_count = 
                                local_audio_playlist_count 
                                - target->getPlaylistCount();
                    }
                    target->dataSwap(
                                    new_metadata, 
                                    metadata_additions,
                                    metadata_deletions,
                                    new_playlists,
                                    playlist_additions,
                                    playlist_deletions
                                    );
                    if(target->isAudio())
                    {
                        metadata_audio_generation_mutex.lock();
                            ++metadata_audio_generation;
                        metadata_audio_generation_mutex.unlock();
                    }
                    if(target->isLocal() && target->isAudio())
                    {
                        local_audio_metadata_count = 
                                local_audio_metadata_count 
                                + target->getMetadataCount();
                        local_audio_playlist_count = 
                                local_audio_playlist_count 
                                + target->getPlaylistCount();
                    }
                local_audio_playlist_count_mutex.unlock();
            local_audio_metadata_count_mutex.unlock();
        }
        else
        {
            //  Crap!
        }
        
    unlockMetadata();
}

MetadataContainer* MetadataServer::getMetadataContainer(int which_one)
{
    //
    //  Hope and pray that whoever called this locked the metadata first!
    //
    
    MetadataContainer *target = NULL;
    MetadataContainer *a_container;
    for (
            a_container = metadata_containers->first(); 
            a_container; 
            a_container = metadata_containers->next()
        )
    {
        if(a_container->getIdentifier() == which_one)
        {
            target = a_container;
            break; 
        }
    }
    return target;        
}



MetadataServer::~MetadataServer()
{
    if(metadata_containers)
    {
        delete metadata_containers;
    }
}
