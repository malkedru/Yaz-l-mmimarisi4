#ifndef PIPELINE_LOADER_H
#define PIPELINE_LOADER_H
#include "StageRegistry.h"
#include "PipelineBuilder.h"
#include "FileLineSource.h"
#include "FileSink.h"
#include "ConsoleSink.h"
#include <fstream>
#include <sstream>
#include <algorithm>

// Owns every component referenced by PipelineBuilder's callbacks.
// A shared owner is captured by the returned Pipeline, so loader temporaries are safe.
struct LoadedComponents {
    std::unique_ptr<Source<std::string> > source;
    std::unique_ptr<Stage<std::string,LogRecord> > parser;
    std::vector<std::unique_ptr<ConfigRecordStage> > stages;
    std::unique_ptr<Sink<LogRecord> > sink;
};

class PipelineLoader {
    StageRegistry registry_;
    static std::string index(size_t i) {std::ostringstream s;s<<"pipeline.stages["<<i<<"]";return s.str();}
    static const ConfigValue& component(const ConfigValue& root,const std::string& key) {
        const ConfigValue& c=root.at(key,"pipeline");c.keys({"type","params"},"pipeline."+key);return c;
    }
    static std::string normalized(std::string s) {
#ifdef _WIN32
        char* absolute = _fullpath(0, s.c_str(), 0);
        if (!absolute) throw ConfigError("path cannot be resolved: " + s);
        s=absolute;
        std::free(absolute);
#endif
        std::replace(s.begin(),s.end(),'\\','/');
        std::vector<std::string> parts;std::istringstream in(s);std::string p;
        while(std::getline(in,p,'/')) {if(p.empty()||p==".") continue;if(p==".." && !parts.empty() && parts.back()!="..") parts.pop_back();else parts.push_back(p);}
        std::string out=(!s.empty()&&s[0]=='/')?"/":"";
        for(size_t i=0;i<parts.size();++i) {if(i) out+='/';out+=parts[i];}
#ifdef _WIN32
        for(size_t i=0;i<out.size();++i) out[i]=static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
#endif
        return out;
    }
public:
    explicit PipelineLoader(const StageRegistry& r=BuiltinStages::registry()) : registry_(r) {}
    Pipeline loadText(const std::string& text) const {
        ConfigValue document=ConfigParser(text).parse();document.keys({"pipeline"},"root");
        const ConfigValue& p=document.at("pipeline","root");
        p.keys({"name","source","stages","sink"},"pipeline");p.at("name","pipeline").string("pipeline.name");
        const ConfigValue& source=component(p,"source");
        if(source.at("type","pipeline.source").string("pipeline.source.type")!="file") throw ConfigError("pipeline.source.type: expected file");
        const ConfigValue& sp=source.at("params","pipeline.source");sp.keys({"path"},"pipeline.source.params");
        std::string input=sp.at("path","pipeline.source.params").string("pipeline.source.params.path");
        if(input.find('\0')!=std::string::npos) throw ConfigError("pipeline.source.params.path: NUL character forbidden");
        std::shared_ptr<LoadedComponents> owned(new LoadedComponents);
        owned->source.reset(new FileLineSource(input));
        const ConfigValue& stages=p.at("stages","pipeline");
        if(stages.kind!=ConfigValue::Array || stages.array.empty()) throw ConfigError("pipeline.stages: non-empty array required; first stage must parse clf");
        std::vector<std::string> fields;
        bool projected=false;
        for(size_t i=0;i<stages.array.size();++i) {
            const ConfigValue& c=stages.array[i];std::string w=index(i);c.keys({"type","params"},w);
            std::string type=c.at("type",w).string(w+".type");
            StageInstance s=registry_.create(type,c.at("params",w),w);
            if(i==0) {
                if(!s.parser || s.record) throw ConfigError(w+": first stage must convert text to records (parse)");
                owned->parser=std::move(s.parser);
            } else {
                if(!s.record || s.parser) throw ConfigError(w+": expected a record stage; parse is only allowed first");
                if(projected) for(size_t j=0;j<s.requiredFields.size();++j) {
                    const std::string& f=s.requiredFields[j];
                    if(std::find(fields.begin(),fields.end(),f)==fields.end()) throw ConfigError(w+": required field '"+f+"' removed by projection; move stage before project");
                }
                if(!s.fields.empty()) {
                    if(projected) for(size_t j=0;j<s.fields.size();++j)
                        if(std::find(fields.begin(),fields.end(),s.fields[j])==fields.end()) throw ConfigError(w+": cannot restore a field removed by earlier project");
                    fields=s.fields;projected=true;
                }
                owned->stages.push_back(std::move(s.record));
            }
        }
        const ConfigValue& sink=component(p,"sink");
        std::string sinkType=sink.at("type","pipeline.sink").string("pipeline.sink.type");
        const ConfigValue& params=sink.at("params","pipeline.sink");
        RecordFormatter formatter=fields.empty()?RecordFormatter(&ConsoleSink::format):LogFields::tabSeparated(fields);
        if(sinkType=="file") {
            params.keys({"path"},"pipeline.sink.params");
            std::string output=params.at("path","pipeline.sink.params").string("pipeline.sink.params.path");
            if(output.find('\0')!=std::string::npos) throw ConfigError("pipeline.sink.params.path: NUL character forbidden");
            if(normalized(input)==normalized(output)) throw ConfigError("pipeline.sink.params.path: source and sink must use different files");
            owned->sink.reset(new FileSink(output,formatter));
        } else if(sinkType=="console") {params.keys({},"pipeline.sink.params");owned->sink.reset(new ConsoleSink(formatter));}
        else throw ConfigError("pipeline.sink.type: expected file or console");
        PipelineStep<LogRecord> chain=PipelineBuilder::from(*owned->source).via(*owned->parser);
        for(size_t i=0;i<owned->stages.size();++i) chain=chain.via(*owned->stages[i]);
        Pipeline built=chain.to(*owned->sink);
        return Pipeline([owned,built]() mutable {built.run();},std::vector<LifecycleHook>());
    }
    Pipeline load(const std::string& path) const {
        std::ifstream in(path.c_str(),std::ios::binary);
        if(!in) throw ConfigError(path+": configuration file cannot be opened");
        std::ostringstream text;text<<in.rdbuf();
        if(in.bad()) throw ConfigError(path+": configuration file read failed");
        try {return loadText(text.str());} catch(const ConfigError& e) {throw ConfigError(path+": "+e.what());}
    }
};
#endif
