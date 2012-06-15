// C++ interface from apache module to Aikido interpreter

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "aikidomod.h"
#include <dirent.h>
#include "aikidointerpret.h"
#include <apr_network_io.h>
#include "aikidoxml.h"
#include "aikidozip.h"

extern bool tracecode;

namespace aikido {
extern bool localimport;

namespace apache {

extern const char *getRootScript();     // generated in rootscript.cc by makeroot.aikido

const char *version = "1.0";
const char *title = "Aikido apache module (mod_aikido)";

extern "C" {

Class *requestClass;
Class *responseClass;

int aikido_debug = 0;

//
// initialize the aikido interpreter and return it to the caller
//
aikidoenv *aikido_init(const char *rootdir, const char *webappdir) {
    Aikido *aikido = new Aikido (rootdir, "mod_aikido", 0, NULL, std::cin, std::cerr, std::cerr,
                aikido::MULTITHREADED | aikido::NOCPP | aikido::TRACEBACK, 0, NULL) ;
    

    std::stringstream f;
    f << getRootScript();

    try {
        ApacheWorker *worker = new ApacheWorker(aikido, webappdir);
        aikido->setWorker (worker);

        // parse the root file
        //std::cerr << "parsing root file\n";
        aikido->parse ("apachemod.aikido", f) ;

        aikido->generate() ;
        aikido->execute() ;

        localimport = true ;        // from this point on, an import will behave locally (import into local tree)
        //tracecode = true;
        //verbosegc = true;

        //std::cerr << "finding root\n";
        int rootlevel ;
        Variable *root = aikido->findVariable ("apacheroot", rootlevel, aikido::VAR_PUBLIC) ;
        if (root == NULL) {
            throw Exception ("Cannot find apacheroot");
        }
        //std::cerr << "found root, getting root value from " << root << "\n";
        aikido::Value rootvalue = root->getValue (aikido->mainStack) ;
        //std::cerr << rootvalue << "\n";
        Object *rootobj = rootvalue.object ;
        //std::cerr << "found root\n";

        Scope *scope ;
        Tag *ap = aikido->findTag (string (".HTTP")) ;
        if (ap == NULL) {
            throw Exception ("Can't find apache package") ;
        }
        Package *apache = (Package *)ap->block ;

        Tag *reqtag = (Tag *)apache->findVariable (string (".Request"), scope, VAR_ACCESSFULL, NULL, NULL) ;
        if (reqtag == NULL) {
            throw Exception ("Can't find HTTP.Request") ;
        }
        requestClass = (Class *)reqtag->block ;

        Tag *restag = (Tag *)apache->findVariable (string (".Response"), scope, VAR_ACCESSFULL, NULL, NULL) ;
        if (restag == NULL) {
            throw Exception ("Can't find HTTP.Response") ;
        }
        responseClass = (Class *)restag->block ;

        std::stringstream initstream;
        initstream << "apache-init\n";


        aikidoenv *env = new aikidoenv();
        env->aikido = aikido;
        env->worker = worker;
        env->root = rootobj;
        std::cerr << title << " version " << version << " initialized\n";
        return env;
    } catch (Exception &e) {
        std::stringstream str;
        aikido->reportException (e, str);
        std::cerr << "mod_aikido: initialization failure: " << str.str() << "\n";
    } catch (const char *s) {
        std::cerr << "mod_aikido: initialization failure:" << s << "\n";
    } catch (...) {
        std::cerr << "mod_aikido: initialization failure due to unknown reason\n";
    }
    return NULL;
}


void aikido_terminate (aikidoenv *env) {
    delete env->worker;
    delete env->aikido;
    delete env;
}

void *aikido_new_request (request_rec *req) {
    Request *r = new Request (req);
    return reinterpret_cast<void*>(r);
}

void aikido_delete_request (void *req) {
    Request *r = reinterpret_cast<Request*>(req);
    delete r;
}


void aikido_run (aikidoenv *env, Request *req, int in, int out) {
    // form an aikido foreign command to execute the requested service
    std::stringstream str;
    str << "apache-service " << in << " " << out << " " << req << "\n";
    if (aikido_debug) {
        std::cerr << "mod_aikido: " << str.str() << "\n";
    }

    try {
        env->aikido->execute (str, env->root, NULL, NULL);
    } catch (Exception &e) {
        std::stringstream str;
        env->aikido->reportException (e, str);
        std::cerr << "mod_aikido: service failure: " << str.str() << "\n";
    } catch (const char *s) {
        std::cerr << "mod_aikido: service failure:  " << s << "\n";
    } catch (...) {
        std::cerr << "mod_aikido: service failure due to unknown reason\n";
    }
}


int aikido_check_uri (aikidoenv *env, void *r) {
    Request *req = reinterpret_cast<Request*>(r);

    std::string url = req->getApacheRequest()->uri;
    if (aikido_debug) {
        std::cerr << "mod_aikido: URL: " << url << "\n";
    }

    // now pass it around the webapps to see who wants it
    std::vector<WebApp*> &webapps = env->worker->getWebApps();

    for (unsigned int i = 0 ; i < webapps.size() ; i++) {
        Servlet *servlet = webapps[i]->checkURL (url);
        if (servlet != NULL) {
            if (aikido_debug) {
                std::cerr << "mod_aikido: found servlet '" << servlet->getName() << "' is willing to give service\n";
            }
            req->setWebApp (webapps[i]);
            req->setServlet (servlet);
            return 1;
        }
    }
    if (aikido_debug) {
         std::cerr << "mod_aikido: no servlet found\n";
    }
    return 0;
}


}


void ApacheWorker::initialize() {
}

void ApacheWorker::skipSpaces (std::string::size_type &ch, std::string line) {
    while (ch < line.size() && isspace(line[ch])) {
        ch++;
    }
}


std::string ApacheWorker::readWord (std::string::size_type &ch, std::string line, char sep) {
    skipSpaces (ch, line);
    std::string word;
    while (ch < line.size() && line[ch] != sep) {
       word += line[ch++];
    }
    return word;
}

std::string ApacheWorker::readString (std::string::size_type &ch, std::string line) {
    skipSpaces (ch, line);
    std::string s;
    if (ch < line.size() && line[ch] == '"') {
        ch++;
        while (ch < line.size()) {
           if (line[ch] == '"') {
               break;
           }
           s = line[ch++];
        }
        ch++;
        return s;
    } else {
        return readWord (ch, line, ' ');
    }
}


int ApacheWorker::readInt (std::string::size_type &ch, std::string line, char sep) {
    skipSpaces (ch, line);
    int n = 0;
    while (ch < line.size() && line[ch] != sep) {
        n = n * 10 + line[ch++] - '0';
    }
    return n;
}

// read an address in hex
void *ApacheWorker::readAddr (std::string::size_type &ch, std::string line, char sep) {
    skipSpaces (ch, line);
    unsigned long n = 0;
    if (ch < (line.size() + 2)) {
        if (line[ch] == '0' && line[ch+1] == 'x') {
           ch += 2;
        }
    }

    while (ch < line.size() && line[ch] != sep) {
        char c = toupper(line[ch++]);
        if (c >= 'A') {
            c = c - 'A' + 10;
        } else {
            c -= '0';
        }
        n = (n << 4) | c;
    }
    //std::cerr << "read addr " << n << "\n";
    return reinterpret_cast<void*>(n);
}

void ApacheWorker::initWebapps(aikido::WorkerContext *ctx) {
    DIR *dir ;
    struct dirent *entry ;

    dir = opendir (webappdir.c_str()) ;
    if (dir == NULL) {
        return;
    }
    while ((entry = readdir (dir)) != NULL) {
        if (strstr(entry->d_name, ".war") != NULL) {
           WebApp *webapp = new WebApp (aikido, webappdir, entry->d_name);
           try {
               webapp->parseWAR (ctx);
           } catch (...) {
               continue;
           }
           webapps.push_back (webapp);
        }
    }
    closedir (dir) ;

}


void ApacheWorker::parse (std::istream &in, int scopeLevel, int pass, std::ostream &os, std::istream &is, aikido::WorkerContext *ctx) {
    std::string line;
    std::getline (in, line);
    if (line == "apache-init") {
        // initialize the apache module
        initWebapps(ctx);
    } else if (line.find("apache-service") != std::string::npos) {
        // service a request
        // this is passed the file descriptors for the input and output pipes followed by the address (in hex) of the Request object

        // first find the end of the apache-service text
        std::string::size_type s = line.find ("apache-service");
        while (s < line.size() && !isspace(line[s])) {
            s++;
        }
      
        while (s < line.size() && isspace(line[s])) {
            s++;
        }
        int infd = readInt (s, line, ' ');
        int outfd = readInt (s, line, ' '); 

        fd_ifstream instream (infd);
        fd_ofstream outstream (outfd);

        if (aikido_debug) {
            std::cerr << "mod_aikido: reading request\n";
        }
        Request *req = reinterpret_cast<Request*>(readAddr (s, line, ' '));

        //std::cerr << "getting path info\n";
        std::string url = req->getApacheRequest()->uri;
        if (aikido_debug) {
            std::cerr << "mod_aikido: URL: " << url << "\n";
        }

        WebApp *webapp = req->getWebApp();
        Servlet *servlet = req->getServlet();
        if (webapp == NULL || servlet == NULL) {
            return;
        }

        webapp->service (servlet, req, instream, outstream, ctx);
    }
}


void ApacheWorker::execute() {
}

void ApacheWorker::finalize() {
}

bool ApacheWorker::isClaimed(std::string word) {
    if (word.find ("apache-") != std::string::npos) {
        return true;
    }
    return false;
}

//
// utility functions
//

void readdir (std::string dir, std::vector<std::string> &files) {
    DIR *d = opendir (dir.c_str()) ;

    if (d == NULL) {
        throw "Failed to read directory";
    }
    for (;;) {
        struct dirent *dirent = ::readdir (d) ;
        if (dirent == NULL) {
            break ;
        }
        std::string name = dirent->d_name ;
        if (name == "." || name == "..") {
           continue ;
        }
        files.push_back (dir + "/" + name) ;
    }
    closedir (d) ;
}


void remove (std::string filename, bool recursive) {
    if (recursive) {
        struct stat st;
        int e = stat (filename.c_str(), &st);
        if (e != 0) {
            throw "Stat failed";
        }
        if (S_ISDIR(st.st_mode)) {
            std::vector<std::string> files ;
            readdir (filename, files) ;
            for (unsigned int i = 0 ; i < files.size() ; i++) {
                remove (files[i], recursive) ;
            }
        }
    }
    if (aikido_debug) {
        std::cerr << "mod_aikido: Removing " << filename << "\n";
    }
    int e = ::remove (filename.c_str()) ;
    if (e != 0) {
        throw "Failed to remove directory";
    }
}

std::string dirname (std::string path) {
    std::string::size_type slash = path.rfind ('/') ;
    if (slash == std::string::npos) {
        return "" ;
    }
    return path.substr (0, slash) ;
}

std::string basename (std::string path) {
    std::string::size_type slash = path.rfind ('/') ;
    if (slash == std::string::npos) {
        return path ;
    }
    return path.substr (slash + 1) ;
}

bool file_exists (std::string filename) {
    struct stat st ;
    return ::stat (filename.c_str(), &st) == 0 ;
}


void split (std::string str, char sep, std::vector<std::string> &result) {
    result.clear() ;
    std::string::size_type i, oldi = 0 ;
    do {
        i = str.find (sep, oldi) ;
        if (i != std::string::npos) {
            std::string s (str.substr (oldi, i - oldi)) ;
            result.push_back (s) ;
            oldi = str.find_first_not_of (sep, i) ;
            if (oldi == std::string::npos) {
                break ;
            }
        } else {
            if (oldi < str.size()) {
                std::string s (str.substr (oldi)) ;
                result.push_back (s) ;
            }
        }
    } while (i != std::string::npos) ;
}


void mkdir (std::string dir, int mode) {
    std::vector<std::string> bits ;
    split (dir, '/', bits) ;
    std::string path = "/" ;
    for (unsigned int i = 0 ; i < bits.size() ; i++) {
        if (bits[i] == "") {
            continue;
        }
        path = path == "/" ? "/" + bits[i] : path + "/" + bits[i] ;
        if (!file_exists (path)) {
            if (aikido_debug) {
                std::cerr << "mod_aikido: Making directory " << path << "\n";
            }
            int e = ::mkdir (path.c_str(), mode) ;
            if (e != 0) {
                throw "Failed to make directory";
            }
        }
    }
}



//
// Web Applications
//
WebApp::WebApp (Aikido *aikido, std::string dir, std::string warfile) : aikido(aikido), dir(dir), warfile(warfile) {
    //std::cerr << "new webapp " << warfile << "\n";
    // XXX: hardcode for now
    std::string::size_type suffix = warfile.find (".war");
    wardir = dir + "/" + warfile.substr (0, suffix);
    warfile = dir + "/" + warfile;
    if (aikido_debug) {
        std::cerr << "mod_aikido: wardir: " << wardir << "\n";
        std::cerr << "mod_aikido: warfile: " << warfile << "\n";
    }

    struct stat filestat, dirstat;
    int e1 = stat (warfile.c_str(), &filestat);
    int e2 = stat (wardir.c_str(), &dirstat);

    if (e1 == 0) {
        if (e2 != 0 || filestat.st_mtime > dirstat.st_mtime) {
            // need to expand the directory
            if (e2 == 0) {
                try {
                    remove (wardir, true);
                } catch (const char *s) {
                    std::cerr << s << "\n";
                }
            }
            try {
                std::vector<std::string> files;
                zip::ZipFile zip (warfile);
                zip.list (files);

                // extract the zip entries to the wardir
                for (unsigned int i = 0 ; i < files.size() ; i++) {
                    std::string pathname = wardir + "/" + files[i];
                    if (aikido_debug) {
                        std::cerr << "mod_aikido: extracting " << pathname << "\n";
                    }
                    std::string dir = dirname (pathname);
                    if (dir != "") {
                        mkdir (dir, 0755);
                    }
                    zip::ZipEntry *entry = zip.open (files[i]);
                    std::ofstream out (pathname.c_str());
                    if (out) {
                        std::stringstream *zs = entry->getStream() ;
                        while (!zs->eof()) {
                            char ch = zs->get();
                            if (!zs->eof()) {
                                out.put (ch);
                            }
                        }
                        delete zs;
                    }
                }
            } catch (const char *s) {
                std::cerr << s << "\n";
            }
        } else {
           if (aikido_debug) {
               std::cerr << "mod_aikido: warfile " << warfile << " is newer than wardir " << wardir << "\n";
           }
        }
    } else {
        if (aikido_debug) {
            std::cerr << "mod_aikido: Failed to stat warfile " << warfile << "\n";
        }
    }
}

WebApp::~WebApp() {
    for (unsigned int i = 0 ; i < servlets.size() ; i++) {
        delete servlets[i];
    }
}


Servlet *WebApp::findServlet (std::string name) {
    //std::cerr << "Looking for servlet " << name << "\n";
    for (unsigned int i = 0 ; i < servlets.size() ; i++) {
        if (servlets[i]->name == name) {
            if (aikido_debug) {
                std::cerr << "mod_aikido: found servlet " << name << "\n";
            }
            return servlets[i];
        }
    }
    if (aikido_debug) {
        std::cerr << "mod_aikido: creating servlet " << name << "\n";
    }
    Servlet *s = new Servlet (name);
    servlets.push_back (s);
    return s;
}

void WebApp::parseServletMapping (XML::Element *tree) {
    std::vector<XML::Element*> &children = tree->getChildren();
    Servlet *servlet = NULL;
    for (unsigned int i = 0 ; i < children.size() ; i++) {
        XML::Element *child = children[i];
        if (child->name == "servlet-name") {
            // will have a single child of XML.text
            XML::Element *namenode = child->find ("XML.text");
            if (namenode != NULL) {
                servlet = findServlet (namenode->getBody());
            }
        } else if (child->name == "url-pattern") {
            if (servlet != NULL) {
                XML::Element *node = child->find ("XML.text");
                if (node != NULL) {
                    ServletMapping *mapping = new ServletMapping (servlet, node->getBody());
                    if (aikido_debug) {
                        std::cerr << "mod_aikido: adding mapping " << node->getBody() << " to servlet " << servlet->name << "\n";
                    }
                    servlet->mappings.push_back (mapping);
                }
            }
        }
    }

}

void WebApp::parseServlet (XML::Element *tree) {
    std::vector<XML::Element*> &children = tree->getChildren();
    Servlet *servlet = NULL;
    for (unsigned int i = 0 ; i < children.size() ; i++) {
        XML::Element *child = children[i];
        if (child->name == "servlet-name") {
            // will have a single child of XML.text
            XML::Element *namenode = child->find ("XML.text");
            if (namenode != NULL) {
                servlet = findServlet (namenode->getBody());
            }
        } else if (child->name == "servlet-class") {
            // ignored for Aikido webapps
        } else if (child->name == "servlet-file") {
            // extension for Aikido webapps
            if (servlet != NULL) {
                XML::Element *namenode = child->find ("XML.text");
                if (namenode != NULL) {
                    std::string filename = namenode->getBody();
                    filename = wardir + "/" + filename;
                    if (aikido_debug) {
                        std::cerr << "mod_aikido: adding file " << filename << " to servlet " << servlet->name << "\n";
                    }
                    servlet->files.push_back(filename);
                }
            }
        }
    }

}

void WebApp::parseWebApp (XML::Element *tree) {
    //std::cerr << "parsing web app\n";
    std::vector<XML::Element*> &children = tree->getChildren();
    for (unsigned int i = 0 ; i < children.size() ; i++) {
        XML::Element *child = children[i];
        //std::cerr << "child name: " << child->name << "\n";
        if (child->name == "servlet") {
            parseServlet (child);
        } else if (child->name == "servlet-mapping") {
            parseServletMapping (child);
        }
    }
}

// parse the WEB-INF/web.xml file in the WAR
void WebApp::parseWebInf() {
    //std::cerr << "Parsing WEB-INF for webapp\n";
    std::string webxml = wardir + "/WEB-INF/web.xml";
    std::ifstream in (webxml.c_str());
    if (in) {
        //std::cerr << "parsing XML\n";
        try {
            XML::Parser p (in);
            XML::Element *tree = p.parseStream ();
            std::vector<XML::Element*> &children = tree->getChildren();
            for (unsigned int i = 0 ; i < children.size() ; i++) {
                //std::cerr << "child name " << children[i]->name << "\n";
                if (children[i]->name == "web-app") {
                    parseWebApp (children[i]);
                    break;
                }
            }
        } catch (XML::XMLException &e) {
            std::cerr << "mod_aikido: XML Exception: " << e.get_text() << "\n";
            throw e.get_text();
        } catch (...) {
            std::cerr << "mod_aikido: caught unknown exception\n";
            throw "Unknown XML error";
        }
    }
}


void WebApp::parseWAR (aikido::WorkerContext *ctx) {
    //std::cerr << "parsing WAR dir " << wardir << "\n";

    parseWebInf();

    for (unsigned int i = 0 ; i < servlets.size() ; i++) {
        parse (servlets[i], ctx->stack, ctx->slink, ctx->currentScope, ctx->currentScopeLevel);
    }
}

Function *WebApp::findServletFunction (Servlet *servlet, std::string name) {
    static char buf[256];
    Scope *s;
    Tag *tag = (Tag*)servlet->object.object->block->findVariable ("." + name, s, VAR_PUBLIC, NULL, NULL);
    if (tag == NULL) {
        return NULL;
    }
    if (tag->block->type != T_FUNCTION) {
        snprintf (buf, sizeof(buf), "The '%s' function must be a function in servlet %s", name.c_str(), servlet->name.c_str());
        throw buf;
    }
    return (Function*)tag->block;
}


void WebApp::parse (Servlet *servlet, StackFrame *stack, StaticLink *slink, Scope *scope, int sl) {
    std::vector<std::string> files = servlet->files;
    if (files.size() == 0) {
        // if no files specifed in the servlet, take them all
        if (aikido_debug) {
            std::cerr << "mod_aikido: no files in servlet XML, guessing\n";
        }
        DIR *dir ;
        struct dirent *entry ;

        dir = opendir (wardir.c_str()) ;
        if (dir != NULL) {
            while ((entry = readdir (dir)) != NULL) {
                //std::cerr << "file: " << entry->d_name << "\n";
                if (strstr(entry->d_name, ".aikido") != NULL) {
                   files.push_back (wardir + "/" + entry->d_name);
                }
            }
            closedir (dir) ;
        } else {
            std::cerr << "mod_aikido: failed to open dir " << wardir << "\n"; }
    }
    if (aikido_debug) {
        std::cerr << "mod_aikido: Servlet " << servlet->name << ": preloading: ";

        for (unsigned int i = 0 ; i < files.size() ; i++) {
            std::cerr << files[i] << " ";
        }
        std::cerr << "\n";
    }
    try {
        std::vector<Value> args;
        servlet->object = aikido->loadFile (files, args, stack, slink, scope, sl, "", &std::cerr, NULL);

        // now find the functions of the servlet
        servlet->serviceFunction = findServletFunction (servlet, "service");
        servlet->doGet = findServletFunction (servlet, "doGet");
        servlet->doPost = findServletFunction (servlet, "doPost");
        servlet->doPut = findServletFunction (servlet, "doPut");
        servlet->doDelete = findServletFunction (servlet, "doDelete");
    } catch (Exception &e) {
        std::stringstream str;
        aikido->reportException (e, str);
        std::cerr << "mod_aikido: failed to load servlet '" << servlet->name << "' due to: " << str.str() << "\n";
    } catch (const char *s) {
        std::cerr << "mod_aikido: failed to load servlet '" << servlet->name << "' due to: " << s << "\n";
    } catch (...) {
        std::cerr << "mod_aikido: failed to load servlet '" << servlet->name << "' due to unknown reason\n";
    }
}

void WebApp::writeErrorPage (Servlet *servlet, Request *req, std::ostream &outstream, std::string error) {
    request_rec *r = req->getApacheRequest();
    r->content_type = "text/html";
    outstream << "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\" \"http://www.w3.org/TR/html4/strict.dtd\">\n";
    outstream << "<html>\n";
    outstream << "<head>\n";
    outstream << "<title>Web Application Failure report</title>\n";
    outstream << "</head>\n";
    outstream << "<body>\n";
    outstream << "<h1>The Web Application has suffered a catastrophic failure</h1>\n";
    std::vector<std::string> lines;
    split (error, '\n', lines);
    for (unsigned int i = 0 ; i < lines.size() ; i++) {
        outstream << "<strong>" << lines[i] << "</strong><br/>\n";
    }
    outstream << "<p/>\n";
   
    outstream << title << " version " << version << "<br/>";

    // output the server signature
    const char *signature = apr_table_get (r->subprocess_env, "SERVER_SIGNATURE");
    if (signature != NULL) {
       outstream << signature << "\n";
    }
    outstream << "</body>\n";
    outstream << "</html>\n";
}


void WebApp::service (Servlet *servlet, Request *req, std::istream &instream, std::ostream &outstream, aikido::WorkerContext *ctx) {
    if (aikido_debug) {
        std::cerr << "mod_aikido: service using servlet " << servlet->name << "\n";
    }
    Block *code = servlet->object.object->block;
    std::vector<Value> args;
    //std::cerr << "executing preload\n";

    // write the servlet name and other info into the subprocess_env of the request
    request_rec *r = req->getApacheRequest();
    apr_table_add (r->subprocess_env, "SERVLET_NAME", servlet->name.c_str());
    apr_table_add (r->subprocess_env, "WEBAPP_WARFILE", warfile.c_str());
    apr_table_add (r->subprocess_env, "WEBAPP_DIR", wardir.c_str());

    char version[32] ;
    sprintf (version, "%.2f", (float)VERSION / 100) ;
    apr_table_add (r->subprocess_env, "AIKIDO_VERSION", version);

    // build request and response objects and add them as args
    Object *reqobj = new (requestClass->stacksize) Object (requestClass, ctx->slink, ctx->stack, ctx->stack->inst) ;
    reqobj->ref++ ;

    reqobj->varstack[0] = Value (reqobj) ;
    // construct the object

    VirtualMachine vm (aikido);
    vm.execute (requestClass, reqobj, ctx->slink, 0) ;

    reqobj->varstack[1] = Value((UINTEGER)req) ;
    args.push_back (reqobj);
    reqobj->ref-- ;
    
    Object *respobj = new (responseClass->stacksize) Object (responseClass, ctx->slink, ctx->stack, ctx->stack->inst) ;
    respobj->ref++ ;

    respobj->varstack[0] = Value (respobj) ;
    // construct the object

    vm.execute (responseClass, respobj, ctx->slink, 0) ;

    respobj->varstack[1] = Value((UINTEGER)req) ;
    args.push_back (respobj);
    respobj->ref-- ;
    

    request_rec *areq = req->getApacheRequest();
    int method =  areq->method_number;
    bool done = false;

    try {
        switch (method) {
        case M_GET:
            if (servlet->doGet != NULL) {
                aikido->call (servlet->object.object, servlet->doGet, args, &outstream, &instream);
                done = true;
            }
            break;
        case M_POST:
            if (servlet->doPost != NULL) {
                aikido->call (servlet->object.object, servlet->doPost, args, &outstream, &instream);
                done = true;
            }
            break;
        case M_PUT:
            if (servlet->doPut != NULL) {
                aikido->call (servlet->object.object, servlet->doPut, args, &outstream, &instream);
                done = true;
            }
            break;
        case M_DELETE:
            if (servlet->doDelete != NULL) {
                aikido->call (servlet->object.object, servlet->doDelete, args, &outstream, &instream);
                done = true;
            }
            break;
        default:;
            // allow default handling
        }

        if (!done) {
            if (servlet->serviceFunction == NULL) {
                static char buf[256];
                snprintf (buf, sizeof(buf), "Cannot find an appropriate service function in servlet %s", servlet->name.c_str());
                throw buf;
            }
            aikido->call (servlet->object.object, servlet->serviceFunction, args, &outstream, &instream); 
        }
    } catch (Exception &e) {
        std::stringstream str;
        aikido->reportException (e, str);
        writeErrorPage (servlet, req, outstream, str.str());
        std::cerr << "mod_aikido: failed to execute servlet '" << servlet->name << "' due to: " << str.str() << "\n";
    } catch (const char *s) {
        writeErrorPage (servlet, req, outstream, s);
        std::cerr << "mod_aikido: failed to execute servlet '" << servlet->name << "' due to: " << s << "\n";
    } catch (...) {
        writeErrorPage (servlet, req, outstream, "unknown failure");
        std::cerr << "mod_aikido: failed to execute servlet '" << servlet->name << "' due to unknown reason\n";
    } 
}


Servlet *WebApp::checkURL (std::string url) {
    for (unsigned int i = 0 ; i < servlets.size() ; i++) {
        if (servlets[i]->match (url)) {
            return servlets[i];
        }
    }
    return NULL;
}

Servlet::~Servlet() {
    for (unsigned int i = 0 ; i < mappings.size() ; i++) {
        delete mappings[i];
    }
}

bool Servlet::match (std::string url) {
    for (unsigned int i = 0 ; i < mappings.size() ; i++) {
        if (mappings[i]->match (url)) {
            return true;
        }
    }
    return false;
}

//
// Servlet Mapping
//

bool ServletMapping::match (std::string url) {
    std::string::size_type i = 0;
    if (aikido_debug) {
        std::cerr << "mod_aikido: matching " << url << " against pattern " << urlpattern << "\n";
    }
    while (i < url.size() && i < urlpattern.size()) {
        if (url[i] != urlpattern[i]) {
            if (urlpattern[i] == '*') {
               i++;
               if (i < urlpattern.size()) {
                   std::string suffix;
                   while (i < urlpattern.size()) {
                      suffix += urlpattern[i++];
                   }
                   std::string::size_type index = url.rfind (suffix);
                   if (index == url.size() - suffix.size()) {
                       if (aikido_debug) {
                           std::cerr << "mod_aikido: suffix matches\n";
                       }
                       return true;
                   } 
                   if (aikido_debug) {
                       std::cerr << "mod_aikido: suffix doesn't match\n";
                   }
                   return false;
               } else {
                   if (aikido_debug) {
                       std::cerr << "mod_aikido: url pattern ends in *, accept\n";
                   }
                   return true;     // ends in *, accept
               }
            } else {
                 if (aikido_debug) {
                     std::cerr << "mod_aikido: character mismatch at index " << i << "\n";
                 }
                 return false;      // character doesn't match
            }
        }
        i++;
    }

    // we get here when we reach the end of the url or the pattern
    if (url.size() == urlpattern.size()) {
        if (aikido_debug) {
            std::cerr << "mod_aikido: match\n";
        }
        return true;
    }

    // no match
    if (aikido_debug) {
        std::cerr << "mod_aikido: no match\n";
    }
    return false;
}

//
// native functions
//

extern "C" {

// Request functions
AIKIDO_NATIVE(internal_getMethod) {
    Request *req = reinterpret_cast<Request*>(paras[1].integer);
    request_rec *areq = req->getApacheRequest();
    return areq->method_number;
}

AIKIDO_NATIVE(internal_getMethodString) {
    Request *req = reinterpret_cast<Request*>(paras[1].integer);
    request_rec *areq = req->getApacheRequest();
    return new string(areq->method);
}

AIKIDO_NATIVE(internal_getArgsString) {
    Request *req = reinterpret_cast<Request*>(paras[1].integer);
    request_rec *areq = req->getApacheRequest();
    if (areq->args == NULL) {
        return new string ("");
    }
    return new string(areq->args);
}

AIKIDO_NATIVE(internal_getURI) {
    Request *req = reinterpret_cast<Request*>(paras[1].integer);
    request_rec *areq = req->getApacheRequest();
    if (areq->uri == NULL) {
        return new string ("");
    }
    return new string (areq->uri);
}

AIKIDO_NATIVE(internal_getFilename) {
    Request *req = reinterpret_cast<Request*>(paras[1].integer);
    request_rec *areq = req->getApacheRequest();
    if (areq->filename == NULL) {
        return new string ("");
    }
    return new string (areq->filename);
}

AIKIDO_NATIVE(internal_getCanonicalFilename) {
    Request *req = reinterpret_cast<Request*>(paras[1].integer);
    request_rec *areq = req->getApacheRequest();
    if (areq->canonical_filename == NULL) {
        return new string ("");
    }
    return new string (areq->canonical_filename);
}

AIKIDO_NATIVE(internal_getRemoteAddr) {
    Request *req = reinterpret_cast<Request*>(paras[1].integer);
    request_rec *areq = req->getApacheRequest();

    const char *addr = areq->connection->remote_ip;
    if (addr == NULL) {
        return new string ("");
    } 
    return new string (addr);
}


// get a map of parsed args
AIKIDO_NATIVE(internal_getArgs) {
    Request *req = reinterpret_cast<Request*>(paras[1].integer);
    request_rec *areq = req->getApacheRequest();
 
    map *m = new map();
    if (areq->args == NULL) {
        return m;
    }
    std::string args =  areq->args;
    std::string::size_type i = 0;
    while (i < args.size()) {
        std::string name;
        std::string value;

        // get the name
        while (i < args.size() && args[i] != '=' && args[i] != '&') {
            name += args[i++];
        }
        if (args[i] == '=') {
            i++;
            // collect value
            while (i < args.size() && args[i] != '&') {
                value += args[i++];
            }
        }
        i++;        // skip terminator

        // insert into map
        const Value key (new string (name));
        const Value val (new string (value));
        m->insert (key, val);
    }
    return m;
}

int header_callback (void *mp, const char *key, const char *value) {
    map *m = reinterpret_cast<map*>(mp);
    const Value keyv (new string (key));
    const Value valv (new string (value));
    m->insert (keyv, valv);
    return 1;
}



// get a map of HTTP headers
AIKIDO_NATIVE(internal_getHeadersIn) {
    Request *req = reinterpret_cast<Request*>(paras[1].integer);
    request_rec *areq = req->getApacheRequest();
    map *m = new map();
    apr_table_do (header_callback, reinterpret_cast<void*>(m), areq->headers_in, NULL);

    return m;
}




// get a map of common vars
AIKIDO_NATIVE(internal_getVars) {
    Request *req = reinterpret_cast<Request*>(paras[1].integer);
    request_rec *areq = req->getApacheRequest();
    map *m = new map();
    apr_table_do (header_callback, reinterpret_cast<void*>(m), areq->subprocess_env, NULL);

    return m;
}



struct FindHeader {
    Value::vector *v;
    bool all;
    std::string name;
};

int find_header_callback (void *vp, const char *key, const char *value) {
    FindHeader *v = reinterpret_cast<FindHeader*>(vp);
    if (strcasecmp (v->name.c_str(), key) == 0) {
        v->v->push_back (Value (value));
        return v->all ? 1 : 0;      // stop if only need one
    }
    return 1;
}



// get a header (paras[3] says whether to get them all or not)
AIKIDO_NATIVE(internal_getHeader) {
    Request *req = reinterpret_cast<Request*>(paras[1].integer);
    request_rec *areq = req->getApacheRequest();
    std::string name = paras[2].str->str;
    bool all = paras[3].integer;

    FindHeader find;
    find.v = new Value::vector();
    find.all = all;
    find.name = name;

    apr_table_do (find_header_callback, reinterpret_cast<void*>(&find), areq->headers_in, NULL);

    if (all) {
        return find.v;
    } else {
        if (find.v->size() == 0) {
            return Value();     // return none
        } else {
            return (*find.v)[0];
        }
    }
}


AIKIDO_NATIVE(internal_getVar) {
    Request *req = reinterpret_cast<Request*>(paras[1].integer);
    request_rec *areq = req->getApacheRequest();
    std::string name = paras[2].str->str;

    FindHeader find;
    find.v = new Value::vector();
    find.all = false;
    find.name = name;

    apr_table_do (find_header_callback, reinterpret_cast<void*>(&find), areq->subprocess_env, NULL);

    if (find.v->size() == 0) {
        return Value();     // return none
    } else {
        return (*find.v)[0];
    }
}



// Response functions

AIKIDO_NATIVE(internal_setContentType) {
    Request *req = reinterpret_cast<Request*>(paras[1].integer);
    string *type = paras[2].str;

    request_rec *areq = req->getApacheRequest();
    ap_set_content_type (areq, type->c_str());
    return 0;
}

AIKIDO_NATIVE(internal_setHeader) {
    Request *req = reinterpret_cast<Request*>(paras[1].integer);
    string *name = paras[2].str;
    string *value = paras[3].str;

    request_rec *areq = req->getApacheRequest();
    apr_table_set (areq->headers_out, name->c_str(), value->c_str());
    return 0;
}

AIKIDO_NATIVE(internal_addHeader) {
    Request *req = reinterpret_cast<Request*>(paras[1].integer);
    string *name = paras[2].str;
    string *value = paras[3].str;

    request_rec *areq = req->getApacheRequest();
    apr_table_add (areq->headers_out, name->c_str(), value->c_str());
    return 0;
}

AIKIDO_NATIVE(internal_setStatus) {
    Request *req = reinterpret_cast<Request*>(paras[1].integer);
    int status = paras[2].integer;

    request_rec *areq = req->getApacheRequest();
    areq->status = status;
    return 0;
}

AIKIDO_NATIVE(internal_decodeURI) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "decodeURI", "Need a string to decode");
    }
    std::string uri = paras[1].str->str;

    std::string result;
    std::string::size_type i = 0;
    while (i < uri.size()) {
        char ch = uri[i];
        if (ch == '%') {
            // collect the hex digits
            char ch1 = toupper(uri[i+1]);
            char ch2 = toupper(uri[i+2]);
            if (ch1 >= 'A') {
                ch1 = ch1 - 'A' + 10;
            } else {
                ch1 -= '0';
            }
            if (ch2 >= 'A') {
                ch2 = ch2 - 'A' + 10;
            } else {
                ch2 -= '0';
            }
            unsigned char n = (ch1 << 4) | ch2;
            result += n;
            i += 3;
        } else {
           result += uri[i++];
        }
    }
    return new string (result);
}


AIKIDO_NATIVE(internal_encodeURI) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "encodeURI", "Need a string to encode");
    }
    std::string uri = paras[1].str->str;
    char buf[16];

    std::string result;
    std::string::size_type i = 0;
    while (i < uri.size()) {
        char ch = uri[i];
        switch (ch) {
        case '!':
        case '*':
        case '\'':
        case '(':
        case ')':
        case ';':
        case ':':
        case '@':
        case '&':
        case '=':
        case '+':
        case '$':
        case ',':
        case '/':
        case '?':
        case '#':
        case '[':
        case ']':
        case ' ':
        case '"':
        case '%':
        case '-':
        case '.':
        case '<':
        case '>':
        case '\\':
        case '^':
        case '_':
        case '`':
        case '{':
        case '|':
        case '}':
        case '~':
           snprintf (buf, sizeof(buf), "%%%02x", ch);
           result += buf;
           break;
        default:
           if (ch < 32 || ch > 126) {
               snprintf (buf, sizeof(buf), "%%%02x", ch);
               result += buf;
           } else {
               result += ch;
           }
        }
    }
    return new string (result);
}

//
// given a URI and a path pattern, parse it and produce a map of variables/values
// the pattern is something like /{pkg}.{cls}/{dn}
//
// the URI consists of a series of components, separated by /.  The pattern is similar but contains
// variable names enclosed in braces.  Between the variable names are characters that must be matched
// in the URI component
AIKIDO_NATIVE(internal_parseURI) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "parseURI", "Need a string for the URI");
    }
    if (paras[2].type != T_STRING) {
        throw newParameterException (vm, stack, "parseURI", "Need a string for the pattern");
    }
    if (paras[3].type != T_STRING) {
        throw newParameterException (vm, stack, "parseURI", "Need a string for the prefix");
    }
    std::string uri = paras[1].str->str;
    std::string pattern = paras[2].str->str;
    std::string prefix = paras[3].str->str;

    std::string::size_type prefixlen = prefix.size();
    // strip the prefix off the URI
    if (prefixlen > 0) {
        // only strip prefix if the URI starts with it
        if (uri.find (prefix) == 0) {
            uri = uri.substr (prefixlen);
        } else {
           throw newException (vm, stack, "URI does not begin with the specified prefix");
        }
    }

    map *m = new map();

    // components in the URI (separated by /)
    std::vector<std::string> uricomponents;
    split (uri, '/', uricomponents);

    std::vector<std::string> patterncomponents;
    split (pattern, '/', patterncomponents);

    if (uricomponents.size() != patterncomponents.size()) {
        throw newException (vm, stack, "Mismatch of URI and pattern component count");
    }

    unsigned int complength = uricomponents.size();
    for (unsigned int comp = 0; comp < complength ; comp++) {
       std::string uricomp = uricomponents[comp];
       std::string patterncomp = patterncomponents[comp];

       std::string::size_type i = 0, j = 0;
       while (i < uricomp.size() && j < patterncomp.size()) {
           if (patterncomp[j] == '{') {
               std::string var;
               j++;
               while (j < patterncomp.size()) {
                   if (patterncomp[j] == '}') {
                       break;
                   }
                   if (patterncomp[j] == '{') {
                      throw newException (vm, stack, "Malformed pattern variable name");
                   }

                   var += patterncomp[j++];
               }
               j++;     // skip close 

               std::string value;
               if (j < patterncomp.size()) {
                   // something follows the variable
                   std::string term;
                   while (j < patterncomp.size()) {
                       if (patterncomp[j] == '{') {
                           break;
                       }
                       term += patterncomp[j++];
                   }

                   // j points to the next variable, or the end of the pattern component

                   // find the terminator in the uri component
                   std::string::size_type index = uricomp.find (term, i);
                   if (index != std::string::npos) {
                       value = uricomp.substr (i, index);
                       i = index + term.size();
                       // i points to the character after the terminator
                   } else {
                       // the terminator was not found in the uri component.
                       throw newException (vm, stack, "URI does not match pattern");
                   }
               } else {
                   // variable ends at the end of uri component
                   while (i < uricomp.size()) {
                       value += uricomp[i++];
                   }
               }

               // insert the variable
               m->insert (Value(new string(var)), Value (new string(value)));
           } else {
               if (uricomp[i] != patterncomp[j]) {
                   // mismatch at an explicit character, drop out
                   throw newException (vm, stack, "URI does not match pattern");
               }
               i++;
               j++;
           }
       }

       // has the URI component been consumed?
       if (i < uricomp.size()) {
           throw newException (vm, stack, "URI does not match pattern");
       }
    }

    return m;
}

AIKIDO_NATIVE(internal_matchURI) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "matchURI", "Need a string for the URI");
    }
    if (paras[2].type != T_STRING) {
        throw newParameterException (vm, stack, "matchURI", "Need a string for the pattern");
    }
    if (paras[3].type != T_STRING) {
        throw newParameterException (vm, stack, "matchURI", "Need a string for the prefix");
    }
    std::string uri = paras[1].str->str;
    std::string pattern = paras[2].str->str;
    std::string prefix = paras[3].str->str;

    std::string::size_type prefixlen = prefix.size();
    // strip the prefix off the URI
    if (prefixlen > 0) {
        // only strip prefix if the URI starts with it
        if (uri.find (prefix) == 0) {
            uri = uri.substr (prefixlen);
        } else {
           return 0;
        }
    }

    // components in the URI (separated by /)
    std::vector<std::string> uricomponents;
    split (uri, '/', uricomponents);

    std::vector<std::string> patterncomponents;
    split (pattern, '/', patterncomponents);

    if (uricomponents.size() != patterncomponents.size()) {
        return 0;
    }

    unsigned int complength = uricomponents.size();
    for (unsigned int comp = 0; comp < complength ; comp++) {
       std::string uricomp = uricomponents[comp];
       std::string patterncomp = patterncomponents[comp];

       std::string::size_type i = 0, j = 0;
       while (i < uricomp.size() && j < patterncomp.size()) {
           if (patterncomp[j] == '*') {
               j++;

               if (j < patterncomp.size()) {
                   // something follows the variable
                   std::string term;
                   while (j < patterncomp.size()) {
                       if (patterncomp[j] == '*') {
                           break;
                       }
                       term += patterncomp[j++];
                   }

                   // j points to the next wildcard, or the end of the pattern component

                   // find the terminator in the uri component
                   std::string::size_type index = uricomp.find (term, i);
                   if (index != std::string::npos) {
                       i = index + term.size();
                       // i points to the character after the terminator
                   } else {
                       // the terminator was not found in the uri component.
                       return 0;
                   }
               } else {
                   // variable ends at the end of uri component
                   i = uricomp.size();
               }
           } else {
               if (uricomp[i] != patterncomp[j]) {
                   // mismatch at an explicit character, drop out
                   return 0;
               }
               i++;
               j++;
           }
       }

       // has the URI component been consumed?
       if (i < uricomp.size()) {
           return 0;
       }
    }

    return 1;
}


}


}
}



