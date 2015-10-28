/*
 * Copyright (C) 2003-2015 The Music Player Daemon Project
 * http://www.musicpd.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"
#include "QueueCommands.hxx"
#include "Request.hxx"
#include "CommandError.hxx"
#include "db/DatabaseQueue.hxx"
#include "db/Selection.hxx"
#include "SongFilter.hxx"
#include "SongLoader.hxx"
#include "DetachedSong.hxx"
#include "LocateUri.hxx"
#include "queue/Playlist.hxx"
#include "PlaylistPrint.hxx"
#include "client/Client.hxx"
#include "client/Response.hxx"
#include "Partition.hxx"
#include "BulkEdit.hxx"
#include "util/ConstBuffer.hxx"
#include "util/UriUtil.hxx"
#include "util/NumberParser.hxx"
#include "util/Error.hxx"
#include "fs/AllocatedPath.hxx"

#include <limits>

#include <string.h>

static CommandResult
AddUri(Client &client, const LocatedUri &uri, Response &r)
{
	Error error;
	DetachedSong *song = SongLoader(client).LoadSong(uri, error);
	if (song == nullptr)
		return print_error(r, error);

	auto &partition = client.partition;
	unsigned id = partition.playlist.AppendSong(partition.pc,
						    std::move(*song), error);
	delete song;
	if (id == 0)
		return print_error(r, error);

	return CommandResult::OK;
}

static CommandResult
AddDatabaseSelection(Client &client, const char *uri, Response &r)
{
#ifdef ENABLE_DATABASE
	const ScopeBulkEdit bulk_edit(client.partition);

	const DatabaseSelection selection(uri, true);
	Error error;
	return AddFromDatabase(client.partition, selection, error)
		? CommandResult::OK
		: print_error(r, error);
#else
	(void)client;
	(void)uri;

	r.Error(ACK_ERROR_NO_EXIST, "No database");
	return CommandResult::ERROR;
#endif
}

CommandResult
handle_add(Client &client, Request args, Response &r)
{
	const char *uri = args.front();
	if (memcmp(uri, "/", 2) == 0)
		/* this URI is malformed, but some clients are buggy
		   and use "add /" to add the whole database, which
		   was never intended to work, but once did; in order
		   to retain backwards compatibility, work around this
		   here */
		uri = "";

	Error error;
	const auto located_uri = LocateUri(uri, &client,
#ifdef ENABLE_DATABASE
					   nullptr,
#endif
					   error);
	switch (located_uri.type) {
	case LocatedUri::Type::UNKNOWN:
		return print_error(r, error);

	case LocatedUri::Type::ABSOLUTE:
	case LocatedUri::Type::PATH:
		return AddUri(client, located_uri, r);

	case LocatedUri::Type::RELATIVE:
		return AddDatabaseSelection(client, located_uri.canonical_uri,
					    r);
	}

	gcc_unreachable();
}

CommandResult
handle_addid(Client &client, Request args, Response &r)
{
	const char *const uri = args.front();

	const SongLoader loader(client);
	Error error;
	unsigned added_id = client.partition.AppendURI(loader, uri, error);
	if (added_id == 0)
		return print_error(r, error);

	if (args.size == 2) {
		unsigned to;
		if (!args.Parse(1, to, r))
			return CommandResult::ERROR;

		PlaylistResult result = client.partition.MoveId(added_id, to);
		if (result != PlaylistResult::SUCCESS) {
			CommandResult ret =
				print_playlist_result(r, result);
			client.partition.DeleteId(added_id);
			return ret;
		}
	}

	r.Format("Id: %u\n", added_id);
	return CommandResult::OK;
}

/**
 * Parse a string in the form "START:END", both being (optional)
 * fractional non-negative time offsets in seconds.  Returns both in
 * integer milliseconds.  Omitted values are zero.
 */
static bool
parse_time_range(const char *p, SongTime &start_r, SongTime &end_r)
{
	char *endptr;

	const float start = ParseFloat(p, &endptr);
	if (*endptr != ':' || start < 0)
		return false;

	start_r = endptr > p
		? SongTime::FromS(start)
		: SongTime::zero();

	p = endptr + 1;

	const float end = ParseFloat(p, &endptr);
	if (*endptr != 0 || end < 0)
		return false;

	end_r = endptr > p
		? SongTime::FromS(end)
		: SongTime::zero();

	return end_r.IsZero() || end_r > start_r;
}

CommandResult
handle_rangeid(Client &client, Request args, Response &r)
{
	unsigned id;
	if (!args.Parse(0, id, r))
		return CommandResult::ERROR;

	SongTime start, end;
	if (!parse_time_range(args[1], start, end)) {
		r.Error(ACK_ERROR_ARG, "Bad range");
		return CommandResult::ERROR;
	}

	Error error;
	if (!client.partition.playlist.SetSongIdRange(client.partition.pc,
						      id, start, end,
						      error))
		return print_error(r, error);

	return CommandResult::OK;
}

CommandResult
handle_delete(Client &client, Request args, Response &r)
{
	RangeArg range;
	if (!args.Parse(0, range, r))
		return CommandResult::ERROR;

	auto result = client.partition.DeleteRange(range.start, range.end);
	return print_playlist_result(r, result);
}

CommandResult
handle_deleteid(Client &client, Request args, Response &r)
{
	unsigned id;
	if (!args.Parse(0, id, r))
		return CommandResult::ERROR;

	PlaylistResult result = client.partition.DeleteId(id);
	return print_playlist_result(r, result);
}

CommandResult
handle_playlist(Client &client, gcc_unused Request args, Response &r)
{
	playlist_print_uris(r, client.partition, client.playlist);
	return CommandResult::OK;
}

CommandResult
handle_shuffle(gcc_unused Client &client, Request args, Response &r)
{
	RangeArg range = RangeArg::All();
	if (!args.ParseOptional(0, range, r))
		return CommandResult::ERROR;

	client.partition.Shuffle(range.start, range.end);
	return CommandResult::OK;
}

CommandResult
handle_clear(Client &client, gcc_unused Request args, gcc_unused Response &r)
{
	client.partition.ClearQueue();
	return CommandResult::OK;
}

CommandResult
handle_plchanges(Client &client, Request args, Response &r)
{
	uint32_t version;
	if (!ParseCommandArg32(r, version, args.front()))
		return CommandResult::ERROR;

	RangeArg range = RangeArg::All();
	if (!args.ParseOptional(1, range, r))
		return CommandResult::ERROR;

	playlist_print_changes_info(r, client.partition,
				    client.playlist, version,
				    range.start, range.end);
	return CommandResult::OK;
}

CommandResult
handle_plchangesposid(Client &client, Request args, Response &r)
{
	uint32_t version;
	if (!ParseCommandArg32(r, version, args.front()))
		return CommandResult::ERROR;

	RangeArg range = RangeArg::All();
	if (!args.ParseOptional(1, range, r))
		return CommandResult::ERROR;

	playlist_print_changes_position(r, client.playlist, version,
					range.start, range.end);
	return CommandResult::OK;
}

CommandResult
handle_playlistinfo(Client &client, Request args, Response &r)
{
	RangeArg range = RangeArg::All();
	if (!args.ParseOptional(0, range, r))
		return CommandResult::ERROR;

	if (!playlist_print_info(r, client.partition, client.playlist,
				 range.start, range.end))
		return print_playlist_result(r,
					     PlaylistResult::BAD_RANGE);

	return CommandResult::OK;
}

CommandResult
handle_playlistid(Client &client, Request args, Response &r)
{
	if (!args.IsEmpty()) {
		unsigned id;
		if (!args.Parse(0, id, r))
			return CommandResult::ERROR;

		bool ret = playlist_print_id(r, client.partition,
					     client.playlist, id);
		if (!ret)
			return print_playlist_result(r, PlaylistResult::NO_SUCH_SONG);
	} else {
		playlist_print_info(r, client.partition, client.playlist,
				    0, std::numeric_limits<unsigned>::max());
	}

	return CommandResult::OK;
}

static CommandResult
handle_playlist_match(Client &client, Request args, Response &r,
		      bool fold_case)
{
	SongFilter filter;
	if (!filter.Parse(args, fold_case)) {
		r.Error(ACK_ERROR_ARG, "incorrect arguments");
		return CommandResult::ERROR;
	}

	playlist_print_find(r, client.partition, client.playlist, filter);
	return CommandResult::OK;
}

CommandResult
handle_playlistfind(Client &client, Request args, Response &r)
{
	return handle_playlist_match(client, args, r, false);
}

CommandResult
handle_playlistsearch(Client &client, Request args, Response &r)
{
	return handle_playlist_match(client, args, r, true);
}

CommandResult
handle_prio(Client &client, Request args, Response &r)
{
	unsigned priority;
	if (!args.ParseShift(0, priority, r, 0xff))
		return CommandResult::ERROR;

	for (const char *i : args) {
		RangeArg range;
		if (!ParseCommandArg(r, range, i))
			return CommandResult::ERROR;

		PlaylistResult result =
			client.partition.SetPriorityRange(range.start,
							  range.end,
							  priority);
		if (result != PlaylistResult::SUCCESS)
			return print_playlist_result(r, result);
	}

	return CommandResult::OK;
}

CommandResult
handle_prioid(Client &client, Request args, Response &r)
{
	unsigned priority;
	if (!args.ParseShift(0, priority, r, 0xff))
		return CommandResult::ERROR;

	for (const char *i : args) {
		unsigned song_id;
		if (!ParseCommandArg(r, song_id, i))
			return CommandResult::ERROR;

		PlaylistResult result =
			client.partition.SetPriorityId(song_id, priority);
		if (result != PlaylistResult::SUCCESS)
			return print_playlist_result(r, result);
	}

	return CommandResult::OK;
}

CommandResult
handle_move(Client &client, Request args, Response &r)
{
	RangeArg range;
	int to;

	if (!args.Parse(0, range, r) || !args.Parse(1, to, r))
		return CommandResult::ERROR;

	PlaylistResult result =
		client.partition.MoveRange(range.start, range.end, to);
	return print_playlist_result(r, result);
}

CommandResult
handle_moveid(Client &client, Request args, Response &r)
{
	unsigned id;
	int to;
	if (!args.Parse(0, id, r) || !args.Parse(1, to, r))
		return CommandResult::ERROR;

	PlaylistResult result = client.partition.MoveId(id, to);
	return print_playlist_result(r, result);
}

CommandResult
handle_swap(Client &client, Request args, Response &r)
{
	unsigned song1, song2;
	if (!args.Parse(0, song1, r) || !args.Parse(1, song2, r))
		return CommandResult::ERROR;

	PlaylistResult result =
		client.partition.SwapPositions(song1, song2);
	return print_playlist_result(r, result);
}

CommandResult
handle_swapid(Client &client, Request args, Response &r)
{
	unsigned id1, id2;
	if (!args.Parse(0, id1, r) || !args.Parse(1, id2, r))
		return CommandResult::ERROR;

	PlaylistResult result = client.partition.SwapIds(id1, id2);
	return print_playlist_result(r, result);
}
