#include <iostream>
#include <sstream>

#include "MatchesCommand.h"

MatchesCommand::MatchesCommand(AdblockPlus::FilterEngine& filterEngine)
  : Command("matches"), filterEngine(filterEngine)
{
}

void MatchesCommand::operator()(const std::string& arguments)
{
  std::istringstream argumentStream(arguments);
  std::string url;
  argumentStream >> url;
  std::string contentType;
  argumentStream >> contentType;
  if (!url.size() || !contentType.size())
  {
    ShowUsage();
    return;
  }

  if (filterEngine.Matches(url, contentType))
    std::cout << "Match" << std::endl;
  else
    std::cout << "No match" << std::endl;
}

std::string MatchesCommand::GetDescription() const
{
  return "Returns the first filter that matches the supplied URL";
}

std::string MatchesCommand::GetUsage() const
{
  return name + " URL CONTENT_TYPE";
}
