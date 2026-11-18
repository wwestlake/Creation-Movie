#include "MovieProjectMediaRegistration.h"

static void runProjectMediaRegistrationTests()
{
    struct Asset { juce::String id, versionId, logicalPath; };
    struct Catalog
    {
        std::vector<Asset> assets;
        const Asset* findByVersionId(const juce::String& version) const
        { for (const auto& asset : assets) if (asset.versionId == version) return &asset; return nullptr; }
    };
    struct Session
    {
        struct Manifest { Catalog assetCatalog; } manifest;
        int registrations = 0;
        bool refreshManifest(juce::String&) { return true; }
        const Manifest& getManifest() const { return manifest; }
        void upsertAssetDescriptor(const Asset& asset)
        { manifest.assetCatalog.assets.push_back(asset); ++registrations; }
    } session;
    Document document;
    Media media; media.id="retry-media"; media.versionId="retry-media@1";
    media.name="Original take"; media.path="Media/original.wav";
    media.info.hasAudio=true; media.info.duration=10;
    document.sequence.media={media};
    const auto before=juce::JSON::toString(document.sequence.toJson());
    bool failListing=true, sourceExists=true;
    int listings=0;
    auto list=[&](juce::StringArray& paths,juce::String& error)
    {
        ++listings;
        if(failListing){error="Injected entry-list failure";return false;}
        if(sourceExists)paths.add(media.path);
        return true;
    };
    auto describe=[](const Media& item){return Asset{item.id,item.versionId,item.path};};
    auto save=[&](juce::String& error)
    {return detail::registerMediaReferences(session,document.sequence,list,describe,error);};
    juce::String error;
    check(!save(error) && error=="Injected entry-list failure","Entry-list failure was accepted as successful registration");
    check(listings==1 && session.registrations==0,"Failed listing changed the catalog");
    check(juce::JSON::toString(document.sequence.toJson())==before,"Failed listing changed native media state");
    check(!document.sequence.media[0].catalogued,"Failed listing made media ineligible for retry");

    failListing=false;error={};
    check(save(error),"Subsequent save did not retry registration");
    const auto* registered=session.manifest.assetCatalog.findByVersionId(media.versionId);
    check(listings==2 && session.registrations==1 && registered!=nullptr,"Recovery did not register exactly one original version");
    check(registered->id==media.id && registered->logicalPath==media.path,"Retry changed source identity or copied media");
    check(document.sequence.media[0].catalogued,"Successful retry did not mark registration");
    Sequence reopened;
    check(Sequence::fromJson(document.sequence.toJson(),reopened,error),error.toRawUTF8());
    check(reopened.media[0].catalogued && reopened.media[0].versionId==media.versionId,"Save/reopen lost successful registration or exact version");
    check(save(error) && listings==2 && session.registrations==1,"Repeated save duplicated registration");

    session.manifest.assetCatalog.assets.clear();failListing=true;
    check(save(error) && listings==2 && session.registrations==1,"Old edit resurrected a removed catalog version");
    check(document.sequence.media[0].catalogued,"Removed catalog version lost its provenance");

    document.sequence.media[0].catalogued=false;failListing=false;sourceExists=false;error={};
    check(!save(error) && !document.sequence.media[0].catalogued,"Missing entry was marked as registered");
    sourceExists=true;error={};
    check(save(error) && session.registrations==2,"Missing entry could not register after recovery");
    session.manifest.assetCatalog.assets[0].logicalPath="Media/different.wav";
    const auto saved=juce::JSON::toString(document.sequence.toJson());
    check(!save(error) && juce::JSON::toString(document.sequence.toJson())==saved,"Catalog identity conflict modified native state");
    std::cout<<"PASS: offline registration list failure, save retry, exact identity, no resurrection and missing-entry recovery\n";
}
