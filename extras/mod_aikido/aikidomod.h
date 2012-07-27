#ifndef aikidomod_h_included
#define aikidomod_h_included

#include "aikido.h"
#include "apr_tables.h"
#include "httpd.h"
#include "http_config.h"
#include "http_request.h"
#include "http_core.h"
#include "http_protocol.h"
#include "http_main.h"
#include "aikidoxml.h"


extern "C" {
namespace aikido {

namespace apache {

class Request;

class Servlet;

class ServletMapping {
    friend class WebApp;
public:
    ServletMapping (Servlet *s, std::string urlpattern) : servlet(s), urlpattern(urlpattern) {}
    ~ServletMapping(){}

    bool match (std::string url);       // does the url match the url pattern
private:
    Servlet *servlet;
    std::string urlpattern;
};

class Servlet {
    friend class WebApp;
public:
    Servlet (std::string nm) : name(nm),serviceFunction(NULL), doGet(NULL),
                   doPost(NULL),doPut(NULL), doDelete(NULL) {}
    ~Servlet();

    bool match (std::string url);       // does the url match a known url pattern
    std::string getName() { return name;}
private:
    std::string name;
    std::vector<std::string> files;
    std::vector<ServletMapping*> mappings;
    //Value code;           // preloaded block containing the code for the servlet
    Value object;
    std::vector<std::string> args;
    Function *serviceFunction;
    Function *doGet;
    Function *doPost;
    Function *doPut;
    Function *doDelete;
};

class WebApp {
public:
    WebApp (Aikido *aikido, std::string dir, std::string warfile);
    ~WebApp();

    void parseWebInf();
    Servlet *checkURL (std::string url);
    void parseWAR (aikido::WorkerContext *ctx);
    void service (Servlet *servlet, Request *req, std::istream &instream, std::ostream &outstream, aikido::WorkerContext *ctx);

    void setVar (std::string name, const Value &v);
    Value getVar (std::string name);

    std::string getDir() { return wardir; }
    Object *getAppObj() { return appobj; }

private:
    void parse (Servlet *servlet,  StackFrame *stack, StaticLink *slink, Scope *scope, int sl);
    Function *findServletFunction (Servlet *servlet, std::string name);

    void writeErrorPage (Servlet *servlet, Request *req, std::ostream &outstream, std::string error);

    Servlet *findServlet (std::string name);
    void parseServletMapping (XML::Element *tree) ;
    void parseServlet (XML::Element *tree) ;
    void parseWebApp (XML::Element *tree) ;

    Aikido *aikido;
    std::string dir;
    std::string warfile;
    std::string wardir;
 
    Object *appobj;     // HTTP.Application object

    std::vector<Servlet*> servlets;

    // global variables shared among all servlets in this webapp
    typedef std::map<std::string, Value> Variables;
    Variables variables;
};

class Request {
public:
    Request(request_rec *req):req(req), servlet(NULL), webapp(NULL){}
    ~Request(){}

    request_rec *getApacheRequest() { return req ;}

    void setServlet(Servlet *s) { servlet = s;}
    void setWebApp(WebApp *w) { webapp = w;}
    Servlet *getServlet() { return servlet;}
    WebApp *getWebApp() { return webapp;}
private:
    void handle (WebApp *app);
    request_rec *req;
    Servlet *servlet;
    WebApp *webapp;
};

class ApacheWorker : public Worker {
public:
    ApacheWorker(Aikido *a, const char *webappdir) : Worker(a),webappdir(webappdir) {}
    void initialize() ;
    void parse (std::istream &in, int scopeLevel, int pass, std::ostream &os, std::istream &is, aikido::WorkerContext *ctx) ;
    void execute() ;
    void finalize() ;
    bool isClaimed(std::string word) ;
 
    std::vector<WebApp*> &getWebApps() { return webapps;}
private:    
    std::string webappdir;

    void checkWebApps();

    void handleRequest (Request *req);

    std::vector<WebApp*> webapps;

    void skipSpaces (std::string::size_type &ch, std::string line);
    std::string readWord (std::string::size_type &ch, std::string line, char terminator);
    int readInt (std::string::size_type &ch, std::string line, char sep);
    void *readAddr (std::string::size_type &ch, std::string line, char sep);
    std::string readString (std::string::size_type &ch, std::string line);

    void initWebapps(aikido::WorkerContext *ctx);
};

struct aikidoenv {
    Aikido *aikido;
    ApacheWorker *worker;
    Object *root;
};


}   // apache namespace
}   // aikido namespace

}   // "C" extern



#endif

