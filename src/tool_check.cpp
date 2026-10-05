#include "tool_check.h"
#include <tree_sitter/api.h>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

extern "C" const TSLanguage * tree_sitter_bash();
namespace {
const std::set<std::string> builtins={"[",":","alias","bg","bind","break","builtin",
    "cd","command","compgen","complete","compopt","continue","declare","dirs",
    "disown","echo","enable","exec","exit","export","false","fc","fg","getopts",
    "hash","help","history","jobs","kill","let","local","logout","mapfile","popd",
    "printf","pushd","pwd","read","readarray","readonly","return","set","shift",
    "shopt","suspend","test","times","trap","true","type","typeset","ulimit",
    "umask","unalias","unset","wait"};

std::string text(TSNode node,const std::string & source) {
    return source.substr(ts_node_start_byte(node),ts_node_end_byte(node)-ts_node_start_byte(node));
}
// Decode only literal shell words. Expansions would require execution or context.
bool literal(const std::string & s,std::string & out) {
    char quote=0;
    for (size_t i=0;i<s.size();++i) {
        char c=s[i];
        if (quote=='\'') {if (c=='\'') quote=0; else out+=c; continue;}
        if (c=='\\') {
            if (++i==s.size()) return false;
            if (quote=='"' && s[i]!='"' && s[i]!='\\' && s[i]!='$' && s[i]!='`') out+='\\';
            out+=s[i]; continue;
        }
        if (c=='"') {quote=quote=='"'?0:'"';continue;}
        if (c=='\'' && !quote) {quote=c;continue;}
        if (c=='$' || c=='`' || (!quote && (c=='*'||c=='?'||c=='['||c=='~'))) return false;
        out+=c;
    }
    return !quote;
}
std::string require_literal(TSNode node,const std::string & source) {
    std::string value;
    if (text(node,source)=="[") return "[";
    if (!literal(text(node,source),value) || value.empty())
        throw std::runtime_error("cannot check a computed command name; nothing executed");
    return value;
}
bool executable(const std::string & name) {
    auto usable=[](const std::string & p) {
        std::error_code ec;
        return std::filesystem::is_regular_file(p,ec) && access(p.c_str(),X_OK)==0;
    };
    if (name.find('/')!=std::string::npos) return usable(name);
    const char * path=std::getenv("PATH");
    std::string paths=path?path:"/usr/bin:/bin";
    size_t begin=0;
    do {
        auto end=paths.find(':',begin);
        auto dir=paths.substr(begin,end==std::string::npos?end:end-begin);
        if (usable((dir.empty()?".":dir)+"/"+name)) return true;
        if (end==std::string::npos) break;
        begin=end+1;
    } while (true);
    return false;
}
void require_tool(const std::string & name,const std::set<std::string> & functions) {
    if (name=="eval" || name=="source" || name==".")
        throw std::runtime_error("cannot check dependencies of evaluated shell text; nothing executed");
    if (!builtins.count(name) && !functions.count(name) && !executable(name))
        throw std::runtime_error("required utility is unavailable: "+name+"; nothing executed");
}
void gather(TSNode node,const std::string & source,std::set<std::string> & functions) {
    if (std::string(ts_node_type(node))=="function_definition")
        functions.insert(text(ts_node_child_by_field_name(node,"name",4),source));
    for (uint32_t i=0;i<ts_node_named_child_count(node);++i)
        gather(ts_node_named_child(node,i),source,functions);
}
void inspect(TSNode node,const std::string & source,const std::set<std::string> & functions) {
    if (std::string(ts_node_type(node))=="command") {
        auto name=require_literal(ts_node_child_by_field_name(node,"name",4),source);
        require_tool(name,functions);
        std::vector<TSNode> args;
        for (uint32_t i=0;i<ts_node_child_count(node);++i) {
            const char * field=ts_node_field_name_for_child(node,i);
            if (field && std::string(field)=="argument") args.push_back(ts_node_child(node,i));
        }
        // Dependencies delegated by common shell/find wrappers are still explicit.
        if (name=="command" || name=="exec" || name=="builtin") {
            for (auto arg:args) {
                auto value=require_literal(arg,source);
                if (value[0]=='-') continue;
                require_tool(value,functions); break;
            }
        }
        if (name=="find") {
            for (size_t i=0;i+1<args.size();++i) {
                std::string value;
                if (literal(text(args[i],source),value) &&
                    (value=="-exec" || value=="-execdir" || value=="-ok" || value=="-okdir"))
                    require_tool(require_literal(args[i+1],source),functions);
            }
        }
    }
    for (uint32_t i=0;i<ts_node_named_child_count(node);++i)
        inspect(ts_node_named_child(node,i),source,functions);
}
}
void check_tools(const std::string & command) {
    std::unique_ptr<TSParser,decltype(&ts_parser_delete)> parser(ts_parser_new(),ts_parser_delete);
    if (!parser || !ts_parser_set_language(parser.get(),tree_sitter_bash()))
        throw std::runtime_error("dependency parser initialization failed");
    std::unique_ptr<TSTree,decltype(&ts_tree_delete)> tree(
        ts_parser_parse_string(parser.get(),nullptr,command.data(),command.size()),ts_tree_delete);
    if (!tree || ts_node_has_error(ts_tree_root_node(tree.get())))
        throw std::runtime_error("could not inspect command dependencies; nothing executed");
    std::set<std::string> functions;
    auto root=ts_tree_root_node(tree.get());
    gather(root,command,functions);
    inspect(root,command,functions);
}
