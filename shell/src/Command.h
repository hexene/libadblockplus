/*
 * This file is part of Adblock Plus <http://adblockplus.org/>,
 * Copyright (C) 2006-2014 Eyeo GmbH
 *
 * Adblock Plus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Adblock Plus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Adblock Plus.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef COMMAND_H
#define COMMAND_H

#include <map>
#include <stdexcept>
#include <string>

class Command
{
public:
  const std::string name;

  explicit Command(const std::string& name);
  virtual ~Command();
  virtual void operator()(const std::string& arguments) = 0;
  virtual std::string GetDescription() const = 0;
  virtual std::string GetUsage() const = 0;

protected:
  void ShowUsage() const;
};

typedef std::map<const std::string, Command*> CommandMap;

class NoSuchCommandError : public std::runtime_error
{
public:
  explicit NoSuchCommandError(const std::string& commandName);
};

#endif
