/*
  This file is part of bitplanes.

  bitplanes is free software: you can redistribute it and/or modify
  it under the terms of the Lesser GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  bitplanes is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  Lesser GNU General Public License for more details.

  You should have received a copy of the Lesser GNU General Public License
  along with bitplanes.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "bitplanes/utils/config_file.h"

#include <algorithm>
#include <sstream>
#include <vector>

static inline std::vector<std::string> split(std::string& str, char delim)
{
  std::vector<std::string> ret;
  std::stringstream ss(str);
  std::string token;
  while(std::getline(ss, token, delim))
    ret.push_back(token);

  return ret;
}

namespace bp {

ConfigFile::ConfigFile() {}

ConfigFile::ConfigFile(std::ifstream& ifs)
{
  if(!ifs.is_open())
    throw Error("input file is not open");

  parse(ifs);
}

ConfigFile::ConfigFile(std::string filename)
{
  std::ifstream ifs(filename);
  if(!ifs.is_open())
    throw Error("could not open file '" + filename + "'");

  parse(ifs);
}

void ConfigFile::parse(std::ifstream& ifs)
{
  std::string line;
  while(!ifs.eof()) {
    std::getline(ifs, line);

    if(line.empty())
      continue;

    if(line.front() == '#' || line.front() == '%')
      continue;

    line.erase(std::remove_if(std::begin(line), std::end(line),
            [](char c) { return std::isspace(c); } ),
        std::end(line));

    std::vector<std::string> tokens = split(line, '=');
    if(tokens.size() != 2)
      throw Error("malformed line " + line);

    _data[tokens[0]] = tokens[1];
  }
}

ConfigFile& ConfigFile::operator()(const std::string& key,
                                   const std::string& value)
{
  _data[key] = value;
  return *this;
}

std::ostream& operator<<(std::ostream& os, const ConfigFile& cf)
{
  for(const auto& it : cf._data) {
    os << it.first << " = " << it.second << std::endl;
  }

  return os;
}

bool ConfigFile::save(std::string filename) const
{
  std::ofstream ofs(filename);
  if(ofs.is_open())
    ofs << *this;
  return !ofs.bad();
}


} // bp

