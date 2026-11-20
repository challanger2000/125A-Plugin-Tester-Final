#pragma once
// E-MU Emulator II text-state semantic comparison. Records key names only,
// never ROM bytes, sample banks, file paths or state values. Unknown states
// deliberately remain inconclusive.
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <string_view>

namespace eii_state_semantics {
struct Verdict {
    bool supported = false;
    bool equivalent = false;
    unsigned formatOnlyFields = 0;
    // Only the recognized absent -> "1" marker transition; not an audio proof.
    bool bootBankPendingIntroduced = false;
    std::string firstDifferentKey;
};
inline bool parse(std::string_view src, std::map<std::string,std::string>& out) {
    constexpr std::string_view magic="EIIVST3 1\n";
    if(src.size()>1024u*1024u || src.substr(0,magic.size())!=magic)return false;
    src.remove_prefix(magic.size());
    bool calibration=false;
    while(!src.empty()){
        const auto end=src.find('\n');
        auto line=src.substr(0,end);
        if(end==std::string_view::npos)src={};
        else src.remove_prefix(end+1);
        if(!line.empty()&&line.back()=='\r')line.remove_suffix(1);
        if(line.empty())continue;
        if(line=="calibration:"){if(calibration)return false;calibration=true;continue;}
        const auto eq=line.find('=');
        if(eq==0||eq==std::string_view::npos)return false;
        const std::string key=std::string(calibration?"calibration.":"")+std::string(line.substr(0,eq));
        if(!out.emplace(key,std::string(line.substr(eq+1))).second)return false;
    }
    return calibration && out.count("levelmodel") && out.count("rom") && out.count("program") &&
        out.count("controls") && out.count("ssm2calibration") && out.count("calibration.EII_FILTER_CALIBRATION");
}
inline bool numericKey(const std::string& key){
    if(key.rfind("calibration.",0)==0 && key!="calibration.EII_FILTER_CALIBRATION")return true;
    return key=="controls"||key=="legacycalibration"||key=="eiiicalibration"||
        key=="ssm2calibration"||key=="volume"||key=="inputgain"||
        key=="sampleinputtrimdb"||key=="postoutputdb";
}
inline bool parseFinite(std::string_view s,double& v){
    if(s.empty())return false;
    const auto r=std::from_chars(s.data(),s.data()+s.size(),v,std::chars_format::general);
    return r.ec==std::errc{} && r.ptr==s.data()+s.size() && std::isfinite(v);
}
inline bool equivalentNumbers(std::string_view a,std::string_view b){
    for(;;){
        const auto commaA=a.find(','),commaB=b.find(',');
        if((commaA==std::string_view::npos)!=(commaB==std::string_view::npos))return false;
        double x=0,y=0;
        if(!parseFinite(a.substr(0,commaA),x)||!parseFinite(b.substr(0,commaB),y))return false;
        const double ulp=4*std::numeric_limits<double>::epsilon()*std::max(std::abs(x),std::abs(y));
        if(!(x==y||std::abs(x-y)<=ulp))return false;
        if(commaA==std::string_view::npos)return true;
        a.remove_prefix(commaA+1);b.remove_prefix(commaB+1);
    }
}
inline Verdict compare(std::string_view source,std::string_view restored){
    Verdict result;
    std::map<std::string,std::string> left,right;
    if(!parse(source,left)||!parse(restored,right))return result;
    result.supported=true;
    // On project recall, the original EII firmware may boot a disk without
    // proving the current sampler RAM bank was captured by original DISK 7.
    // P014 deliberately emits exactly one new bankpending=1 safety marker.
    // Accept ONLY this status transition, and ONLY if every other persisted
    // field is proven equivalent; never hide a changed path/filter/parameter.
    const auto pending=right.find("bankpending");
    const bool bootPending=left.find("bankpending")==left.end() &&
        pending!=right.end() && pending->second=="1" &&
        right.size()==left.size()+1;
    if(left.size()!=right.size() && !bootPending){
        for(const auto& [key,value]:left)
            if(right.find(key)==right.end()){result.firstDifferentKey=key;return result;}
        for(const auto& [key,value]:right)
            if(left.find(key)==left.end()){result.firstDifferentKey=key;return result;}
        result.firstDifferentKey="field count";return result;
    }
    for(const auto& [key,value]:left){
        const auto found=right.find(key);
        if(found==right.end()){result.firstDifferentKey=key;return result;}
        if(value==found->second)continue;
        if(!numericKey(key)||!equivalentNumbers(value,found->second)){
            result.firstDifferentKey=key;return result;
        }
        ++result.formatOnlyFields;
    }
    result.bootBankPendingIntroduced=bootPending;
    result.equivalent=true;
    return result;
}
inline bool selfTest() {
    auto state=[](const std::string& value,const std::string& path){
        return std::string("EIIVST3 1\nrom=")+path+
            "\nprogram=-1\nlevelmodel=2\ncontrols=0,0,0,0,2.5,0"
            "\nssm2calibration=22.6,13.5\ncalibration:"
            "\nEII_FILTER_CALIBRATION=5\nFcRin="+value+"\n";
    };
    const auto a=state("22.6","C:/ROM");
    const auto rounded=state("22.600000000000001","C:/ROM");
    const auto numeric=compare(a,rounded);
    auto bootPending=a;
    const auto beforeCalibration=bootPending.find("calibration:");
    if(beforeCalibration==std::string::npos)return false;
    bootPending.insert(beforeCalibration,"bankpending=1\n");
    auto alteredFilter=state("23.6","C:/ROM");
    alteredFilter.insert(alteredFilter.find("calibration:"),"bankpending=1\n");
    auto badFlag=bootPending;
    badFlag.replace(badFlag.find("bankpending=1"),
                    std::string("bankpending=1").size(),"bankpending=0");
    const auto boot=compare(a,bootPending);
    // Positive: metadata-only boot transition. Negative: reverse direction,
    // bogus value, real filter change, extra unrelated persisted fields.
    return boot.supported && boot.equivalent && boot.bootBankPendingIntroduced &&
        !compare(bootPending,a).equivalent && !compare(a,badFlag).equivalent &&
        !compare(a,alteredFilter).equivalent &&
        !compare(a,bootPending+"hddpending=1\n").equivalent &&
        numeric.supported && numeric.equivalent && numeric.formatOnlyFields==1 &&
        compare(a,a).equivalent &&
        !compare(a,state("23.6","C:/ROM")).equivalent &&
        compare(a,state("23.6","C:/ROM")).firstDifferentKey=="calibration.FcRin" &&
        !compare(a,state("22.6","C:/Different")).equivalent &&
        compare(a,state("22.6","C:/Different")).firstDifferentKey=="rom" &&
        !compare(a,a+"rom=duplicate\n").equivalent &&
        !compare("UNRELATED",a).supported;
}
} // namespace eii_state_semantics
