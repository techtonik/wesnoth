/* $Id$ */
/*
   Copyright (C) 2003 by David White <davidnwhite@optusnet.com.au>
   Part of the Battle for Wesnoth Project http://wesnoth.whitevine.net

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY.

   See the COPYING file for more details.
*/
#include "actions.hpp"
#include "ai.hpp"
#include "ai_attack.hpp"
#include "ai_move.hpp"
#include "dialogs.hpp"
#include "game_config.hpp"
#include "log.hpp"
#include "pathfind.hpp"
#include "playlevel.hpp"
#include "playturn.hpp"
#include "replay.hpp"
#include "show_dialog.hpp"

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <sstream>

replay recorder;

namespace {

replay* random_generator = &recorder;

struct set_random_generator {

	set_random_generator(replay* r) : old_(random_generator)
	{
		random_generator = r;
	}

	~set_random_generator()
	{
		random_generator = old_;
	}

private:
	replay* old_;
};

}

int get_random()
{
	return random_generator->get_random();
}

const config* get_random_results()
{
	return random_generator->get_random_results();
}

void set_random_results(const config& cfg)
{
	random_generator->set_random_results(cfg);
}

replay::replay() : pos_(0), current_(NULL), skip_(0)
{}

replay::replay(config& cfg) : cfg_(cfg), pos_(0), current_(NULL), skip_(0)
{}

config& replay::get_config()
{
	return cfg_;
}

void replay::set_save_info(const game_state& save)
{
	saveInfo_ = save;
}

const game_state& replay::get_save_info() const
{
	return saveInfo_;
}

void replay::set_skip(int turns_to_skip)
{
	skip_ = turns_to_skip;
}

void replay::next_skip()
{
	if(skip_ > 0)
		--skip_;
}

bool replay::skipping() const
{
	return skip_ != 0;
}

void replay::save_game(game_data& data, const std::string& label, const config& start_pos,
					   bool include_replay)
{
	log_scope("replay::save_game");
	saveInfo_.starting_pos = start_pos;

	if(include_replay)
		saveInfo_.replay_data = cfg_;
	else
		saveInfo_.replay_data = config();

	saveInfo_.label = label;

	::save_game(saveInfo_);

	saveInfo_.replay_data = config();
	saveInfo_.starting_pos = config();
}

void replay::add_recruit(int value, const gamemap::location& loc)
{
	config* const cmd = add_command();

	config val;

	char buf[100];
	sprintf(buf,"%d",value);
	val["value"] = buf;

	sprintf(buf,"%d",loc.x+1);
	val["x"] = buf;

	sprintf(buf,"%d",loc.y+1);
	val["y"] = buf;

	cmd->add_child("recruit",val);
}

void replay::add_recall(int value, const gamemap::location& loc)
{
	config* const cmd = add_command();

	config val;

	char buf[100];
	sprintf(buf,"%d",value);
	val["value"] = buf;

	sprintf(buf,"%d",loc.x+1);
	val["x"] = buf;

	sprintf(buf,"%d",loc.y+1);
	val["y"] = buf;

	cmd->add_child("recall",val);
}

void replay::add_movement(const gamemap::location& a,const gamemap::location& b)
{
	add_pos("move",a,b);
	current_ = NULL;
}

void replay::add_attack(const gamemap::location& a, const gamemap::location& b,
                        int weapon)
{
	add_pos("attack",a,b);
	char buf[100];
	sprintf(buf,"%d",weapon);
	current_->child("attack")->values["weapon"] = buf;
}

void replay::add_pos(const std::string& type,
                     const gamemap::location& a, const gamemap::location& b)
{
	config* const cmd = add_command();

	config move, src, dst;

	char buf[100];
	sprintf(buf,"%d",a.x+1);
	src["x"] = buf;
	sprintf(buf,"%d",a.y+1);
	src["y"] = buf;
	sprintf(buf,"%d",b.x+1);
	dst["x"] = buf;
	sprintf(buf,"%d",b.y+1);
	dst["y"] = buf;

	move.add_child("source",src);
	move.add_child("destination",dst);
	cmd->add_child(type,move);

	current_ = cmd;
}

void replay::add_value(const std::string& type, int value)
{
	config* const cmd = add_command();

	config val;

	char buf[100];
	sprintf(buf,"%d",value);
	val["value"] = buf;

	cmd->add_child(type,val);
}

void replay::choose_option(int index)
{
	add_value("choose",index);
}

void replay::end_turn()
{
	config* const cmd = add_command();
	cmd->add_child("end_turn");
}

void replay::speak(const config& cfg)
{
	add_command()->add_child("speak") = cfg;
}

config replay::get_data_range(int cmd_start, int cmd_end)
{
	log_scope("get_data_range\n");

	config res;

	const config::child_list& cmd = commands();
	while(cmd_start < cmd_end) {
		res.add_child("command",*cmd[cmd_start]);
		++cmd_start;
	}

	return res;
}

void replay::undo()
{
	const config::child_itors& cmd = cfg_.child_range("command");
	if(cmd.first != cmd.second) {
		delete *(cmd.second-1);
		cfg_.remove_child("command",cmd.second - cmd.first - 1);
	}
}

const config::child_list& replay::commands() const
{
	return cfg_.get_children("command");
}

int replay::ncommands()
{
	return commands().size();
}

void replay::mark_current()
{
	if(current_ != NULL) {
		(*current_)["mark"] = "yes";
	}
}

config* replay::add_command()
{
	return current_ = &cfg_.add_child("command");
}

int replay::get_random()
{
	if(current_ == NULL) {
		std::cerr << "no context to place random number in\n";
		throw error();
	}

	//random numbers are in a 'list' meaning that each random
	//number contains another random numbers unless it's at
	//the end of the list. Generating a new random number means
	//nesting a new node inside the current node, and making
	//the current node the new node
	config* const random = current_->child("random");
	if(random == NULL) {
		const int res = rand();
		current_ = &current_->add_child("random");

		char buf[100];
		sprintf(buf,"%d",res);
		(*current_)["value"] = buf;

		return res;
	} else {
		const int res = atol((*random)["value"].c_str());
		current_ = random;
		return res;
	}
}

const config* replay::get_random_results() const
{
	assert(current_ != NULL);
	return current_->child("results");
}

void replay::set_random_results(const config& cfg)
{
	assert(current_ != NULL);
	current_->clear_children("results");
	current_->add_child("results",cfg);
}

void replay::start_replay()
{
	pos_ = 0;
}

config* replay::get_next_action()
{
	if(pos_ >= commands().size())
		return NULL;

	std::cerr << "up to replay action " << pos_ << "/" << commands().size() << "\n";

	current_ = commands()[pos_];
	++pos_;
	return current_;
}

bool replay::at_end() const
{
	return pos_ >= commands().size();
}

void replay::set_to_end()
{
	pos_ = commands().size();
	current_ = NULL;
}

void replay::clear()
{
	cfg_ = config();
	pos_ = 0;
	current_ = NULL;
	skip_ = 0;
}

bool replay::empty()
{
	return commands().empty();
}

void replay::add_config(const config& cfg)
{
	for(config::const_child_itors i = cfg.child_range("command");
	    i.first != i.second; ++i.first) {
		cfg_.add_child("command",**i.first);
	}
}

bool do_replay(display& disp, const gamemap& map, const game_data& gameinfo,
               std::map<gamemap::location,unit>& units,
			   std::vector<team>& teams, int team_num, const gamestatus& state,
			   game_state& state_of_game, replay* obj)
{
	log_scope("do replay");
	replay& replayer = (obj != NULL) ? *obj : recorder;

	const set_random_generator generator_setter(&replayer);

	update_locker lock_update(disp,replayer.skipping());

	//a list of units that have promoted from the last attack
	std::deque<gamemap::location> advancing_units;

	team& current_team = teams[team_num-1];

	for(;;) {
		config* const cfg = replayer.get_next_action();

		config* child;

		//if we are expecting promotions here
		if(advancing_units.empty() == false) {
			if(cfg == NULL || (child = cfg->child("choose")) == NULL) {
				std::cerr << "promotion expected, but none found\n";
				throw replay::error();
			}

			const std::map<gamemap::location,unit>::iterator u = units.find(advancing_units.front());
			assert(u != units.end());

			const std::string& num = (*child)["value"];
			const int val = atoi(num.c_str());

			const std::vector<std::string>& options = u->second.type().advances_to();
			if(size_t(val) >= options.size()) {
				std::cerr << "illegal advancement type\n";
				throw replay::error();
			}

			advance_unit(gameinfo,units,advancing_units.front(),options[val]);

			advancing_units.pop_front();
		}

		//if there is nothing more in the records
		else if(cfg == NULL) {
			replayer.set_skip(0);
			return false;
		}

		//if there is an end turn directive
		else if(cfg->child("end_turn") != NULL) {
			replayer.next_skip();
			return true;
		}

		else if((child = cfg->child("recruit")) != NULL) {
			const std::string& recruit_num = (*child)["value"];
			const int val = atoi(recruit_num.c_str());

			const gamemap::location loc(*child);

			const std::set<std::string>& recruits = current_team.recruits();
			std::set<std::string>::const_iterator itor = recruits.begin();
			std::advance(itor,val);
			const std::map<std::string,unit_type>::const_iterator u_type = gameinfo.unit_types.find(*itor);
			if(u_type == gameinfo.unit_types.end()) {
				std::cerr << "recruiting illegal unit\n";
				throw replay::error();
			}

			unit new_unit(&(u_type->second),team_num,true);
			const std::string& res = recruit_unit(map,team_num,units,new_unit,loc);
			if(!res.empty()) {
				std::cerr << "cannot recruit unit: " << res << "\n";
				throw replay::error();
			}

			current_team.spend_gold(u_type->second.cost());
		}

		else if((child = cfg->child("recall")) != NULL) {
			std::sort(state_of_game.available_units.begin(),
			          state_of_game.available_units.end(),
			          compare_unit_values());

			const std::string recall_num = (*child)["value"];
			const int val = atoi(recall_num.c_str());

			const gamemap::location loc(*child);

			if(val >= 0 && val < int(state_of_game.available_units.size())) {
				recruit_unit(map,team_num,units,state_of_game.available_units[val],loc);
				state_of_game.available_units.erase(state_of_game.available_units.begin()+val);
				current_team.spend_gold(game_config::recall_cost);
			} else {
				std::cerr << "illegal recall\n";
				throw replay::error();
			}
		}

		else if((child = cfg->child("move")) != NULL) {

			const config* const destination = child->child("destination");
			const config* const source = child->child("source");

			if(destination == NULL || source == NULL) {
				std::cerr << "no destination/source found in movement\n";
				throw replay::error();
			}

			const gamemap::location src(*source);
			const gamemap::location dst(*destination);

			std::map<gamemap::location,unit>::iterator u = units.find(src);
			if(u == units.end()) {
				std::cerr << "unfound location for source of movement: "
				          << (src.x+1) << "," << (src.y+1) << "-"
						  << (dst.x+1) << "," << (dst.y+1) << "\n";
				throw replay::error();
			}

			const bool ignore_zocs = u->second.type().is_skirmisher();
			const bool teleport = u->second.type().teleports();

			paths paths_list(map,gameinfo,units,src,teams,ignore_zocs,teleport);
			paths_wiper wiper(disp);

			if(!replayer.skipping()) {
				disp.set_paths(&paths_list);

				disp.scroll_to_tiles(src.x,src.y,dst.x,dst.y);
			}

			unit current_unit = u->second;
			units.erase(u);

			std::map<gamemap::location,paths::route>::iterator rt =
			                             paths_list.routes.find(dst);
			if(rt == paths_list.routes.end()) {
				for(rt = paths_list.routes.begin(); rt != paths_list.routes.end(); ++rt) {
					std::cerr << "can get to: " << (rt->first.x+1) << "," << (rt->first.y+1) << "\n";
				}

				std::cerr << "src cannot get to dst: " << current_unit.movement_left() << " "
				          << paths_list.routes.size() << " " << (src.x+1)
				          << "," << (src.y+1) << "-" << (dst.x+1) << ","
				          << (dst.y+1) << "\n";
				throw replay::error();
			}

			rt->second.steps.push_back(dst);

			if(!replayer.skipping())
				disp.move_unit(rt->second.steps,current_unit);

			current_unit.set_movement(rt->second.move_left);
			u = units.insert(std::pair<gamemap::location,unit>(dst,current_unit)).first;
			if(map.underlying_terrain(map[dst.x][dst.y]) == gamemap::TOWER) {
				const int orig_owner = tower_owner(dst,teams) + 1;
				if(orig_owner != team_num) {
					u->second.set_movement(0);
					get_tower(dst,teams,team_num-1,units);
				}
			}

			if(!replayer.skipping()) {
				disp.draw_tile(dst.x,dst.y);
				disp.update_display();
			}

			game_events::fire("moveto",dst);
			if(team_num != 1 && (teams.front().uses_shroud() || teams.front().uses_fog()) && !teams.front().fogged(dst.x,dst.y)) {
				game_events::fire("sighted",dst);
			}

			clear_shroud(disp,map,gameinfo,units,teams,team_num-1);
		}

		else if((child = cfg->child("attack")) != NULL) {

			const config* const destination = child->child("destination");
			const config* const source = child->child("source");

			if(destination == NULL || source == NULL) {
				std::cerr << "no destination/source found in attack\n";
				throw replay::error();
			}

			const gamemap::location src(*source);
			const gamemap::location dst(*destination);

			const std::string& weapon = (*child)["weapon"];
			const int weapon_num = atoi(weapon.c_str());

			std::map<gamemap::location,unit>::iterator u = units.find(src);
			if(u == units.end()) {
				std::cerr << "unfound location for source of attack\n";
				throw replay::error();
			}

			if(size_t(weapon_num) >= u->second.attacks().size()) {
				std::cerr << "illegal weapon type in attack\n";
				throw replay::error();
			}

			std::map<gamemap::location,unit>::const_iterator tgt = units.find(dst);

			if(tgt == units.end()) {
				std::cerr << "unfound defender for attack: "
				          << (src.x+1) << "," << (src.y+1) << " -> "
						  << (dst.x+1) << "," << (dst.y+1) << "\n";
				throw replay::error();
			}

			game_events::fire("attack",src,dst);

			u = units.find(src);
			tgt = units.find(dst);

			if(u != units.end() && tgt != units.end()) {
				attack(disp,map,src,dst,weapon_num,units,state,gameinfo,false);
				check_victory(units,teams);
			}

			u = units.find(src);
			tgt = units.find(dst);

			if(u != units.end() && u->second.advances() &&
			   u->second.type().advances_to().empty() == false) {
				advancing_units.push_back(u->first);
			}

			if(tgt != units.end() && tgt->second.advances() &&
			   tgt->second.type().advances_to().empty() == false) {
				advancing_units.push_back(tgt->first);
			}
		} else if((child = cfg->child("speak")) != NULL) {
			dialogs::unit_speak(*child,disp,units);
		} else {
			std::cerr << "unrecognized action: '" << cfg->write() << "'\n";
			throw replay::error();
		}
	}

	return false; /* Never attained, but silent a gcc warning. --Zas */
}
