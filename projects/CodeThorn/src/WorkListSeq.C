// We want to avoid calling critical sections from critical sections:
// therefore all worklist functions do not use each other.

#include <list>
#include <algorithm>
#include "WorkListSeq.h"

using namespace std;

template<typename Element>
bool SPRAY::WorkListSeq<Element>::isEmpty() { 
  bool res;
  res=(workList.size()==0);
  return res;
}

template<typename Element>
bool SPRAY::WorkListSeq<Element>::exists(Element elem) {
  typename list<Element>::iterator findIter;
  findIter=std::find(workList.begin(), workList.end(), elem);
  return findIter==workList.end();
}

template<typename Element>
void SPRAY::WorkListSeq<Element>::add(Element elem) { 
  workList.push_back(elem); 
}

template<typename Element>
void SPRAY::WorkListSeq<Element>::add(std::set<Element>& elemSet) { 
  for(typename std::set<Element>::iterator i=elemSet.begin();i!=elemSet.end();++i) {
    workList.push_back(*i);
  }
}

template<typename Element>
Element SPRAY::WorkListSeq<Element>::take() {
  if(workList.size()==0) {
    throw "Error: attempted to take element from empty work list.";
  }  else {
    Element co;
    co=*workList.begin();
    workList.pop_front();
    return co;
  }
}

template<typename Element>
Element SPRAY::WorkListSeq<Element>::examine() {
  if(workList.size()==0)
    throw "Error: attempted to examine next element in empty work list.";
  Element elem;
  if(workList.size()>0)
    elem=*workList.begin();
  return elem;
}

