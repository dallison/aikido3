#include "aikidoxml.h"
#include "aikido.h"
#include <stdarg.h>

namespace aikido {
namespace XML {

std::string trim (std::string s) {
    std::string::size_type i = 0 ;
    std::string::size_type len = s.size() ;
    std::string::size_type start=0, end=0, old_end = len - 1;
    while (i < len) {
        if (!isspace (s[i])) {
            start = i ;
            break ;
        }
        i++ ;
    }
    if (i == len) {
        return "" ;
    }
    end = i = len - 1 ;
    while (i > start) {
        if (!isspace (s[i])) {
            end = i ;
            break ;
        }
        i-- ;
    }
    if (0 != start || old_end != end) {
        return s.substr (start, end-start+1) ;
    } else {
        return s;
    }
}

XMLException::XMLException() {
}

XMLException::XMLException (const char *format,...) {
    va_list ap ;
    va_start (ap, format) ;

    char buf[4096] ;
    vsnprintf (buf, sizeof(buf), format, ap) ;
    va_end (ap) ;
    text = buf ;
}

XMLException::~XMLException() {
}


Element::Element (std::string name) : name(name), isempty(false) {
}

Element::~Element() {
    for (unsigned int i = 0 ; i < children.size() ; i++) {
        delete children[i] ;
    }
    for (unsigned int i = 0 ; i < attributes.size() ; i++) {
        delete attributes[i] ;
    }
}

void Element::dump (std::ostream &os, int indent) {
}

void Element::print (std::ostream &os, int indent) {
    if (name.size() > 4 && name.find_first_of("XML.") == 0) {               // internal element
        os << body ;

        for (unsigned int i = 0 ; i < children.size() ; i++) {
            children[i]->print (os, indent + 4) ;
        }
    } else {
        for (int i = 0 ; i < indent ; i++) {
            os << ' ' ;
        }
        os << '<' << name ;
        for (unsigned int i = 0 ; i < attributes.size() ; i++) {
            attributes[i]->print (os) ;
        }
        if (isempty) {
            os << "/>\n" ;
            return ;
        }
        os << ">\n" ;
        if (body != "") {
            for (int i = 0 ; i < indent ; i++) {
                os << ' ' ;
            }
            os << body ;
        }
        for (unsigned int i = 0 ; i < children.size() ; i++) {
            children[i]->print (os, indent + 4) ;
        }
        os << '\n' ;
        for (int i = 0 ; i < indent ; i++) {
            os << ' ' ;
        }
        os << "</" <<  name << ">\n" ;
    }

}

std::string Element::getAttribute (std::string name) {
    for (unsigned int i = 0 ; i < attributes.size() ; i++) {
        if (attributes[i]->name == name) {
            return attributes[i]->value ;
        }
    }
    throw XMLException ("No such attribute %s", name.c_str()) ;
}

bool Element::attributePresent (std::string name) {
    for (unsigned int i = 0 ; i < attributes.size() ; i++) {
        if (attributes[i]->name == name) {
            return true ;
        }
    }
    return false ;
}

Attribute *Element::findAttribute (std::string name) {
    for (unsigned int i = 0 ; i < attributes.size() ; i++) {
        if (attributes[i]->name == name) {
            return attributes[i] ;
        }
    }
    return NULL ;
}

void Element::addBody (std::string s) {
    std::string trimmed = trim (s) ;
    if (trimmed == "") {
        return ;
    }
    Element *bodyelement = new Element ("XML.text") ;
    bodyelement->setBody (s) ;
    children.push_back (bodyelement) ;
}

void Element::addAttribute (std::string name, std::string value) {
    attributes.push_back (new Attribute (name, value)) ;
}

void Element::addChild (Element *child) {
    children.push_back (child) ;
}

Element *Element::find (std::string nm) {
    if (name == nm) {
        return this ;
    }
    for (unsigned int i = 0 ; i < children.size() ; i++) {
        Element *child = children[i] ;
        Element *r = child->find (nm)  ;
        if (r != NULL) {
            return r ;
        }
    }
    return NULL ;
}

std::vector<Element*> &Element::getChildren() {
    return children ;
}



char Parser::get_char() {
    char ch = str.get() ;
    if (ch == '\n') {
        lineno++ ;
    }
    return ch ;
}

bool Parser::readToElement (std::string &result) {
    bool wasElement = false ;
    while (!str.eof()) {
        char ch = get_char() ;
        if (str.eof()) {
            break ;
        }
        if (ch == '<') {
            wasElement = true ;
            break ;
        }
        result += ch ;
    }
    return wasElement ;
}


char Parser::skipSpaces (char lastchar) {
    if (lastchar != '\0' && lastchar != '\n' && lastchar != '\r' && !isspace (lastchar)) {
        return lastchar ;
    }
    while (!str.eof()) {
        char ch = get_char() ;
        if (!(isspace (ch) || ch == '\n' || ch == '\r')) {
            lastchar = ch ;
            break ;
        }
    }
    return lastchar ;
}

void Parser::readName (std::string &name, char &lastchar) {
    char ch = skipSpaces(lastchar) ;
    name += ch ;
    while (!str.eof()) {
        ch = get_char() ;
        if (isspace (ch) || ch == '\n' || ch == '\r' || ch == '>' || ch == '=' || ch == '/') {
            lastchar = ch ;
            break ;
        }
        name += ch ;
    }
    lastchar = skipSpaces(lastchar) ;
    name = trim(name) ;
}

std::string Parser::readAttributeValue (char &lastchar) {
    std::string value = "" ;
    char ch = skipSpaces() ;
    if (ch == '"') {            // string value
        while (!str.eof()) {
            char ch = get_char() ;
            if (ch == '"') {
                lastchar = get_char () ;
                break ;
            }
            value += ch ;
        }
    } else if (ch == '>') {
        throw XMLException ("Missing XML attribute value") ;
    } else {
        value += ch ;
        while (!str.eof()) {
            char ch = get_char() ;
            if (isspace(ch) || ch == '/' || ch == '>') {
                lastchar = ch ;
                break ;
            }
            value += ch ;
        }
    }
    lastchar = skipSpaces (lastchar) ;
    return value ;
}


Element *Parser::parse() {
    std::stack<Element*> stack ;            // main xml stack

    stack.push (new Element ("XML.top")) ;               // top of the XML tree

    std::string initial = "" ;
    bool foundElement = readToElement (initial) ;
    stack.top()->addBody (initial) ;            // before first element is part of top

    while (!str.eof()) {
        if (foundElement) {                 // are we looking at an element?
            std::string name = "" ;
            char termchar = '\0' ;            // terminating char of last read
            readName (name, termchar) ;       // read element name

            if (name.size() > 0) {          // any name?
                unsigned int len = name.size() ;
                if (len >= 3 && name.find_first_of("!--") == 0) {           // comment?
                    bool doskip = true ;
                    if (name[len-2] == '-' && name[len-1] == '-') {        // ends in --?
                        if (termchar == '>') {
                            doskip = false ;
                        }
                    } 
                    if (doskip) {
                        while (!str.eof()) {
                           if (termchar == '-') {
                              termchar = get_char() ;
                              if (termchar == '-') {
                                  termchar = get_char() ;
                                  if (termchar == '>') {
                                     break ;
                                  }
                              }
                           } else {
                               termchar = get_char() ;
                           }
                        }
                    }
                } else if (name == "?xml") {
                    // xml id tag
                    while (!str.eof()) {
                        char ch = get_char() ;
                        if (ch == '>') {
                            break ;
                        }
                    }
               } else if (name == "![CDATA[") {
                    std::string cdata = "";
                    while (!str.eof()) {
                        if (termchar == ']') {
                            termchar = get_char();
                            if (termchar == ']') {
                                termchar = get_char();
                                if (termchar == '>') {
                                    break;
                                } else {
                                    cdata +=  std::string("]]") + termchar;
                                }
                            } else {
                                cdata += std::string("]") + termchar;
                            }
                        } else {
                            cdata +=  termchar;
                        }
                        termchar = get_char();
                    }
                    Element *element = new Element ("XML.CData");
                    element->setBody (cdata);
                    stack.top()->addChild (element);

                } else if (name[0] == '/') {           // end of an element?
                    name = name.substr(1) ;
                    Element *top = stack.top() ;
                    if (top->name == name) {
                        stack.pop() ;
                    } else {
                        throw XMLException ("Element %s is not at the top of the stack", name.c_str()) ;
                    }
                } else {                            // start of an element
                    Element *element = new Element (name) ;
                    stack.top()->addChild (element) ;

                    bool empty = false ;
                    while (termchar != '>') {            // collect attributes
                        std::string attrname = "";
                        readName (attrname, termchar) ;
                        if (attrname == "/" && termchar == '>') {
                             empty = true ;
                             element->setEmpty() ;
                        } else if (termchar == '=') {
                            std::string attrvalue = readAttributeValue (termchar) ;
                            element->addAttribute (attrname, attrvalue) ;
                        } else {
                            element->addAttribute (attrname, "") ;            // no value
                        }
                    }
                    if (!empty) {
                        stack.push (element) ;
                    }
                }
            }
        }

        std::string body = "" ;
        foundElement = readToElement (body) ;
        if (body != "") {
            stack.top()->addBody (body) ;
        }
    }
    return stack.top() ;
}



Element *Parser::parseStream() {
    try {
        return parse() ;
    } catch (XMLException &e) {
        throw e.get_text();
    }
}


}

}

