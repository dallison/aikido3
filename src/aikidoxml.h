#ifndef xml_h_included
#define xml_h_included

#include <ctype.h>
#include <iostream>
#include <stack>
#include <string>
#include <vector>

namespace aikido {

namespace XML {

class XMLException {
public:
    XMLException() ; 
    XMLException (const char *tag, ...) ;
    virtual ~XMLException() ;
    std::string get_text() const { return text ; }
private: 
    std::string text ;
};

class Attribute {
public:
    Attribute (std::string name, std::string value) : name(name), value(value) {}
    std::string name ;
    std::string value ;

    void print (std::ostream &os) {
        os << ' ' << name << "=\"" << value << '"' ;
    }
} ;

class Element {
public:
    Element (std::string name)  ;
    ~Element() ;

    void dump (std::ostream &os, int indent=0) ;
    void print (std::ostream &os, int indent=0) ;

    void addBody (std::string s) ;
    std::string getBody() { return body ; }
    void setBody (std::string b) { body = b ; }
    void setEmpty() { isempty = true ; }
    std::string getAttribute (std::string name) ;
    bool attributePresent (std::string name) ;
    Attribute *findAttribute (std::string name) ;
    void addAttribute (std::string name, std::string value) ;
    void addChild (Element *child) ;
    Element *find (std::string name) ;
    std::vector<Element*> &getChildren() ;
    std::vector<Attribute *> &getAttributes() { return attributes ; }
    std::string name ;
private:
    std::vector<Attribute *> attributes ;
    std::string body ;
    std::vector<Element*> children ;
    bool isempty ;     

} ;

class Parser {
public:
    Parser(std::istream &str) : lineno(1), str(str) {}
    Element *parseStream() ;

private:
    int lineno ;
    std::istream &str ;

    char get_char() ;
    bool readToElement (std::string &result) ;
    char skipSpaces (char lastchar = '\0') ;
    void readName (std::string &name, char &lastchar) ;
    std::string readAttributeValue (char &lastchar) ;
    Element *parse() ;
} ;

}

}

#endif

