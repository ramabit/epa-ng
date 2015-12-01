#ifndef EPA_SEQUENCE_H_
#define EPA_SEQUENCE_H_

#include <string>

#include "pllhead.hpp"

class Sequence
{
public:
  Sequence() = default;
  Sequence(std::string header, std::string sequence);

  Sequence(const Sequence& s) = default;
  Sequence(Sequence&& s) = default;

  Sequence& operator = (const Sequence& s) = default;
  Sequence& operator = (Sequence&& s) = default;

  ~Sequence();

  // member access
  inline const std::string& header() const {return header_;}
  inline const std::string& sequence() const {return sequence_;}

  // operator overloads

private:
  std::string header_;
  std::string sequence_;

};

#endif
