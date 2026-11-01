# lrs — lastread sync

**lrp** — lastread sync protocol.

Русская версия: [README_RU.md](README_RU.md).

At home the node keeps the echoes in Squish and you read them in GoldED.
On the road or at work you read the same mail in FidoMailMobile, set up
on the point address: it gets the mail from the node. `lrs` keeps the
cursor of each echo in step between the node and the point. Read an echo
up to some message at home, and the editor on the point lands on that
same message. Read further on the road, and GoldED at home opens the
echo there.

The editor moves the cursor. The tosser tosses, the mailer transfers the
mail. `lrs` stays out of that. After the toss and before the mailer
session it only lines up the lastreads: where you stopped on one end of
the link, the cursor on the other end follows, when that reading wins.

Squish and JAM are supported for now, and they may be used independently
on both ends. Any other third-party editor, not GoldED, that cannot run
`lrs` can embed the client half and keep its own base. The contract it
has to meet is [lrp](#lrp--lastread-sync-protocol), later in this manual.
The node still runs `lrs`.

## Running

When new mail is coming in, `lrs sync` goes in the batch after the
tosser, on the node and on the point. One pass first looks at what the
mailer has already received, and only then, if there is something to
give the other end, leaves it one file to transfer.

Inbound first. Inbound holds the snapshots received from the other end.
`lrs` copies those cursors into its own bases and deletes a file it has
already taken in.

Outbound second. The editor moved a cursor, or the echo list changed on
the point, so `lrs` has to be run after you leave the editor. The program
writes one snapshot into outbound and adds it to the `.flo`. The mailer
transfers that file.

The `.flo` name is built from the `peer` address, BSO style: one outbound
directory for the whole link, and the zone is not part of the path. Node
`2:203/910` uses `outbound/00cb038e.flo`. Point `2:203/910.1` uses
`outbound/00cb038e.pnt/00000001.flo`. The snapshot itself sits next to
that, in outbound: `sNNNNNNN.lrs` from the node, `cNNNNNNN.lrs` from the
point. While the mailer holds the `.bsy`, `lrs` leaves the flow file
alone: it is already open.

`lrs` writes only the lastread. Squish high-water and JAM `HighReadMsg`
stay with the editor. A message with no MSGID is not a cursor and is
not sent to the other end.

The clocks on the node and on the point are better kept in step. If both
ends have read the same echo before they have talked, whoever read later
wins. The time in the snapshot is when the cursor moved, not when the
file was built.

No `lrs.state` yet means the first run. The point asks the node about
each new echo and does not send the cursor it has at the moment. The node
answers with the place its own cursor is standing. After that, ordinary
reading goes to the other end and is applied there by the rules in the
protocol section.

## Commands

`lrs.conf` in the current directory is used unless `-c` names another
file.

```
lrs sync [-c lrs.conf]
lrs show [-c lrs.conf]
lrs selftest
```

`sync` is the pass described above. Exit status 0 means it finished.
Status 1 means it stopped, and a snapshot it did not take in is still in
inbound for the next run. Status 2 means the command was not recognized.

`show` prints where the cursor is: the echo tag, the local message
number, and the MSGID. It does not take anything in, send anything out,
or move a cursor.

`selftest` runs the built-in checks on temporary bases. It does not read
a live `lrs.conf` and does not touch live echoes.

Text goes to stderr. If `log` is set, the same lines are appended to
that file with the local time in front.

## Configuration

`lrs.conf.example` is a commented sample. A line that starts with `#` is
skipped. Quote a path that contains spaces. The same `lrs` runs on both
ends. The node is the config that has `server yes`. The point leaves
that line out.

| Key | What it is for |
| --- | --- |
| `address` | This machine. Every snapshot is signed with it. |
| `peer` | The other end. A snapshot from anyone else is skipped. Also names the `.flo` and `.bsy`. |
| `inbound` | Where the mailer drops snapshots it received. |
| `outbound` | Where `lrs` puts its snapshot and the flow line. |
| `state` | The program's memory, `lrs.state`. The editor does not read it. |
| `log` | If set, the log is also appended to a file. |
| `echodir` | Where the bases live. A short `echoarea` line means `echodir/<tag>`. |
| `squishuser` | Which Squish user is ours. GoldED's squish user number. Default 0, the first. |
| `jamuser` | The GoldED name stored in a JAM `.jlr`. |
| `server yes` | Node only. Without this line the machine is the point and sends `c` files. |
| `[areas]` | From here to the end of the file, the echo list. |

An echo is `echoarea TAG` or `echoarea TAG path`. The path is the base
name without `.sqd` or `.jhr`. Whichever complete pair is on disk gets
opened: Squish (`.sqd` and `.sqi`) or JAM (`.jhr` and `.jdx`). Both pairs
for one name are logged and left alone. New lines are appended under
`[areas]`. Nothing above that header is rewritten.

On the point, `sync` picks up new `.sqd` and `.jhr` files in `echodir`
and asks the node about those cursors in the same pass. On the node an
echo appears only when the point named its tag and the base is already
there. A tag missing from the point's newest snapshot leaves the node's
config. The base on disk stays. If the directory that should hold the
base is missing, the line stays: the drive may simply not be mounted.
The directory is there but the base files are not: the echo is
unsubscribed and the line goes away.

You can leave `jamuser` out when the `.jlr` has one live user. That user
is used. Several users and no name, or no record yet and no name, and
that echo waits. The others still sync. The cursor is written once
GoldED creates the single record, or once you set `jamuser`. A record
`lrs` creates uses the CRC GoldED looks up, and `HighReadMsg` stays 0.

## lrp — lastread sync protocol

The rest is how to talk to the node when your own editor does not keep
mail in Squish or JAM. Only the snapshot file crosses the link. Message
numbers, high-water, and how the base sits on disk stay at home.

The node runs ordinary `lrs` with `server yes`. The editor embeds the
point half and reads and writes the cursor in its own base. How, in
[Base operations](#base-operations). It does not need the `lrs` sources
or an `lrs.state` file. Any memory is fine if the fields are the same.

Keep the clocks with the node. When the two readings disagree, the time
of the move is what gets compared, in ordinary seconds.

## Who is who

A link has two addresses. The examples use node `2:203/910` and point
`2:203/910.1`. They compare as ordinary FTN addresses, by the numbers:
zone, net, node, point. No point means zero. So `2:203/910.1` is greater
than `2:203/910`, and a tie goes to the point.

| | Node (server) | Client (point) |
| --- | --- | --- |
| Address in `"from"` | its own | its own |
| Snapshot names | `sNNNNNNN.lrs` | `cNNNNNNN.lrs` |
| Subscription list | follows the client's newest file | the file is the list |
| First contact | baselines its current cursor as generation 1 | asks, and does not publish its own cursor yet |
| `seen` 0 while it already has a generation | keeps its cursor | accepts only when it has never applied a server cursor |

Each echo is on its own. The generation is how many times this end moved
the cursor in that echo. It is not one counter for the whole link, and
it is not the digit in the file name.

## What a cursor is

A cursor is "I read up to this message". In the file the message is
named by its MSGID, not by its number in the base. The numbers differ
on the node and on the point. The text is the same.

* The MSGID is compared as it stands, byte for byte, at most 200 bytes.
  Letter case matters. Do not parse it as an address.
* `"-"` means the cursor is still before the first message.
* The local message number, Squish, JAM, or your own, is not put in the
  file. It stays local, so you can see that the editor moved.
* No MSGID means it is not a cursor. The last place you already agreed
  on stays where it was.

The node takes the MSGID apart as described below. Your editor has to
come out with the same string, or the two ends will be talking about
different messages.

Squish, from the control block of the frame, kludges separated by SOH
(`0x01`). The keyword is the five characters `MSGID` (case-sensitive),
then an optional colon and whitespace. The value runs to the next SOH,
CR, LF, or NUL, with trailing spaces and tabs removed. The first such
kludge in the block wins. Longer values are cut to 200 bytes.

JAM, from the message header, not from the body. Subfield LoID 4 is the
MSGID. If that subfield exists, it wins over a kludge. Otherwise the
first subfield LoID 2000 whose text starts with `MSGID:` or `MSGID ` is
used, with that prefix removed. The same trim and 200-byte cut apply.
Deleted messages and index holes are not cursors.

`date` is when the message was written. It does not decide who read
later. It matters only when the message is already gone from the base:
the cursor then sits on the oldest one still there. The node takes the
Squish stamp, or the FTSC date string when that stamp is empty. In JAM
it is `DateWritten`. Your editor should send the same moment. No date
is `"-"`.

## Snapshot file

A snapshot is not "what changed". It is the whole picture: every echo
this end is talking about right now. The mailer transfers it as an
ordinary file. It does not matter whether it arrived by `.flo` or by a
plain copy. `lrs` picks up the matching files in inbound and deletes the
ones it has taken in. A `"from"` that is not the address in `peer` is
skipped. Letter case in the address does not matter.

### Names

```
c0000001.lrs    client to node
s0000001.lrs    node to client
```

The letter is small, the digits are padded with zeros. Zero is never a
name. The counter starts at `0000001` and after `9999999` starts at one
again. It moves only when a file is really written. The node and the
point each have their own. The digit is there so a mailer does not glue
two different snapshots with one name into one file. It has nothing to
do with where the cursor sits.

If the mailer renames a file to `c0000001.lrs.1`, that is still a
snapshot. The point sends `c` and waits for `s` from the node.

One snapshot sits in outbound at a time. A new file replaces the old
one that has not left yet. While `.bsy` is there, leave the flow file
alone: the mailer already has it open. Another transport should do the
same and not replace a file under the sender.

### JSON

`"lrp"` must be `2`. Unknown keys are skipped, so a later field does
not make an older node reject the file. A client must not put protocol
data only into an unknown key. Strings are bytes. Printable ASCII is
written as itself. `"`, `\`, and the usual controls use JSON escapes.
Any other byte is `\u00XX` and is read back as that byte. A `\u` code
point above 255 is rejected. The file is not UTF-8.

Times are `YYYY-MM-DDThh:mm:ssZ` (UTC), or the string `"-"` when the
value is absent. Absent is not the same as zero: zero is
`1970-01-01T00:00:00Z`.

```json
{
  "lrp": 2,
  "from": "2:203/910.1",
  "echo": [
    {
      "tag": "RU.GOLDED",
      "gen": 14,
      "seen": 9,
      "date": "1997-08-19T12:00:00Z",
      "updated": "2026-10-30T17:40:12Z",
      "request": 0,
      "msgid": "2:203/910.1 93d75b8f"
    }
  ]
}
```

| Field | Meaning |
| --- | --- |
| `lrp` | Format version. Only `2`. |
| `from` | Sender's FTN address. |
| `tag` | Echo tag, one path component. Compared case-insensitively. The same tags the node already uses. |
| `gen` | How many times the sender has moved this cursor. `0` is not a cursor. |
| `seen` | The peer generation the sender had already applied when it made this move. |
| `date` | Written time of `msgid`, or `"-"`. |
| `updated` | When the user moved the cursor, Unix seconds in the UTC string above. Not the time the file was built. |
| `request` | `1` asks for the peer's cursor. The other fields are then not a position. `0` on a normal record. |
| `msgid` | Identity of the message, or `"-"`. |

Whitespace between tokens is allowed. The reference writer uses the
shape above.

### A question, and a line with no cursor

The point's sync file must always list every echo in its config.
Otherwise the node drops the missing echo from its own exchange list.
It does not delete the base on disk. The same rule is in
[Which echoes stay in sync](#which-echoes-stay-in-sync).

While there is nothing to say about the cursor, the echo is still in
that list as its own line. The line names no message: the echo was just
added, the base will not open this pass, or the node's answer was just
written and you have not read since.

```json
{"tag":"RU.GOLDED","gen":0,"seen":0,"date":"-","updated":"-","request":0,"msgid":"-"}
```

A separate case is the question. The echo is new, and the point asks
once where the node's cursor is. The question has no place of its own:

```json
{"tag":"RU.GOLDED","gen":0,"seen":0,"date":"-","updated":"-","request":1,"msgid":"-"}
```

The node answers with its own cursor in the same pass. The counter does
not grow because of the answer. The node itself does not send lines
without a cursor: the point owns the list.

## Local store of the instance

The local store is `lrs.state`. If the client software is not `lrs`, the
local store may be arranged some other way. What matters is that whatever
is needed survives from one run to the next. Empty memory is the first
run: every echo starts at generation 0.

Per echo:

| Field | Meaning |
| --- | --- |
| `msgid`, `date`, local id | Cursor last published or last applied. The local id is the base's own key and is never sent. |
| `gen` | Generations this side has published. Grows by one when the user moves. Accepting a remote cursor does not grow it. It returns to 0 only when the echo is dropped from memory (unsubscribe, or the state is deleted). The next local move then publishes 1. |
| `seen` | `peergen` at the moment `gen` last grew. Copied into the snapshot. |
| `peergen` | Highest remote `gen` already decided (accepted or kept). A snapshot with `gen` less than or equal to this is a replay. |
| `updated` | Timestamp stored with the current cursor. |
| `pending` | A remote cursor that could not be written yet, with its `msgid`, `date`, `gen`, `seen`, `updated`, and `from`. |
| `ask`, `asked` | Client only. `ask` means the node has not yet given us a cursor. `asked` means that request is already in a queued file. Do not ask again on later syncs. |
| `reply` | The peer needs the cursor we already store. The next export includes it and does not bump `gen`. |
| `hold` | A remote cursor was taken for a new echo and is not in the base yet. Do not publish the local position until that write succeeds. |

Also remembered: the number of the last file (`sendseq`) and the name
of the snapshot that has not left yet.

Drop an echo from the subscription and delete the base, and it is
forgotten from the local store too. Subscribe again and the counter
starts at zero.

`gen` only grows. Do not zero it while the echo stays subscribed. The
ceiling `0xffffffff` is not reached in practice. If it ever is, that
pass stops.

## What one pass does

The node and the point do the same thing in one run: look at inbound
first, then send at most one file.

The names are `cNNNNNNN.lrs` and `sNNNNNNN.lrs`. A tail such as `.1`,
which a mailer sometimes adds, is still fine. A foreign address and a
broken file stay on disk.

On the node the point's newest file is its whole echo list. No tag in
the file means no echo in the config. The base on disk stays. An empty
list means no echoes left. One file is usually on the way. If an old
one is still sitting in inbound and the counter has started again at
one, the old number looks larger.

A new echo appears on the node only when the point named it and the
base is already there. The node does not look through its directory.
Nothing remembered yet about an echo, and the node stores its current
cursor as generation 1. A cursor before the first message is not stored
that way: the point may set that echo.

If the cursor has already moved, or a remote one was already taken, the
base is read before the incoming file is judged. A move the file has
not told about yet still takes part in the dispute. The point does not
read a brand new echo here. It asks the node first, instead of sending
the place a rescan left behind.

Several records may arrive for one echo, or an earlier one never got
written. One is kept: newer generation, then later time, then greater
address. [Who wins](#who-wins) decides the rest. "Where is the cursor"
is not a dispute. The node just answers. Files that were taken in are
deleted.

Then the outbound file. On a new echo the point asks once. If a file is
written for some other reason, the question stays in it.

Moved the cursor, and the generation grows by one, once in the pass.
The time is when it moved. In `lrs` that is the time of the `.sql` or
`.jlr` file. Do not use the time the snapshot was built. That would
reward whoever ran the program later, not whoever read later.

A cursor just taken from the other end is not our own reading and is
not sent back. Our place is sent again when the other end has not seen
it yet. The generation does not grow because of that.

The point names every echo it has, even with no cursor and even if the
base will not open. Silence looks like an unsubscribe to the node. The
node writes only echoes that already have a generation.

Nothing to say, and no new file is written. One already sitting in
outbound is left alone. The point dropped an echo, and a file goes out
anyway, even with an empty list. Otherwise the node never hears about
the unsubscribe.

## Who wins

Each echo is decided on its own: keep our cursor, or sit on theirs.
The generations are there so an old snapshot is not applied again, and
so merely opening an echo does not wipe a real reading with a newer
file time.

```
decide(local_gen, peergen, remote_gen, remote_seen,
       local_updated, remote_updated,
       local_addr, remote_addr, local_is_server)
```

`peergen` is the highest generation from the other end that we have
already looked at.

1. Their generation is 0, or we have already seen it. Do nothing. This
   is not written to the log. A repeat looks like this, and so does the
   line "still subscribed".
2. They moved, and they already knew our latest cursor. Sit on their
   message. This is also why a point that has no generation of its own
   yet takes the node's first answer.
3. They have never seen our cursor, and we already have a generation.
   Open an echo and close it, and the lastread file looks newer even
   though you read earlier. The clock does not decide this.
   * The node stays where it is.
   * The point yields to the node only the first time, before it has
     ever written the node's cursor. After that, look at the clock.
     Yielding later makes each end take the other's place and stop: an
     accepted cursor is not sent back.
4. Otherwise whoever read later wins. An empty time is older than any
   real date.
5. The time is the same: the greater address wins. In the example in
   this text, a tie stays with the point.

What that means:

* Do nothing. The base stays.
* Stay on ours, and remember that we have seen this snapshot. Send our
  place again, or the other end remains on the message that lost.
* Sit on their message. Our generation does not grow: this was not our
  reading. Next time the program sees the same MSGID and does not tell
  anyone about it as a new move.

The time of an accepted cursor becomes ours. The next reading of our own
replaces it with the time of that reading.

## When there is nowhere to write the message

The MSGID is looked up as it stands.

* The message is there. The cursor sits on it.
* The snapshot says `"-"`. The cursor sits before the first message.
* The message is gone, and by its date it is older than everything
  still in the base. The tosser packed it out. Sit on the oldest one
  left, and remember that one. Otherwise the next pass thinks this is a
  new reading.
* The message is not here yet, and it is not older than the base. The
  toss usually has not arrived. Remember the cursor and try next time.
  The other echoes do not wait. The file is not a failure because of this.
* The editor is holding the base. Wait the same way. Do not write half a
  lastread.
* It is not clear which user record should hold the cursor. Wait, and do
  not invent a user. One echo like that does not stop the whole pass.

While the place is not written yet, do not send our own reading out, and
do not drop the question to the node. Next time the waiting cursor is in
the running again and can lose to a newer snapshot.

A disk error is different. The pass stops, the snapshot stays in
inbound, and we try again.

Squish high-water and JAM `HighReadMsg` do not move. Your editor moves
only the lastread too.

## Which echoes stay in sync

The point decides the list. Letter case in a tag does not matter.

Every sync file from the point must list every echo in its config.
Dropped an echo from the config, and a file still goes out, even if
nobody moved a cursor. The node trusts the newest file as the whole
list. An echo that is not in it leaves the node's exchange list. The
base is not deleted.

A new echo appears on the node only when the point named it and the base
files are already there. The node does not scan its own directory. On
the point a new echo can come from its own directory or from the node's
snapshot. Until it has asked the node and got an answer, it does not
send its own place.

Unsubscribe, and the echo is forgotten in memory too. Subscribe again,
and the introduction starts over.

## The first exchange

The editors on both ends are already sitting on some message. Even when
it is the same message, the point does not send its place. After a
rescan the cursor is often there only because the echo was opened.

1. The point asks. Its own MSGID is not in the question.
2. The node remembers its current lastread as generation 1, when the
   message has a MSGID. The question itself does not move the node's
   cursor.
3. The node answers with that place. The generation does not grow
   because of the answer.
4. The point writes that message locally. It still has no generation of
   its own: it has not read anything since the introduction.
5. The point has nothing to send. The base holds the same message it
   already remembers.

After that, reading on the point goes out as generation 1, and the file
shows that the node's answer was already in. Reading on the node goes
out as generation 2. Until the node has taken a cursor from the point,
its snapshot says `seen` 0.

## When both ends were reading

After the introduction both opened the same echo and read up to
different messages. Each ran `lrs sync` while the other file was still
on the way.

The node tells about its new place. The point tells about its own. Who
ran first, and whose file arrived first, does not change the result.

Whoever read later stays. The time is the same, and the point stays,
because `2:203/910.1` is a greater address than the node's. The end that
lost sits on the winner's message and does not send it back. The winner
sends its place once more, so the other end is sure to see it. The loser
does not look at that repeat: it has already seen that generation.

If only one end moved, we sit there. That includes a move backwards and
a move to before the first message. Rewriting the lastread onto the same
message, with only a newer file time, is not reading. The generation
does not grow.

Two files that pass each other on the way do not get in each other's
way. Look at the cursor already in the base, even if the snapshot about
it has not been sent yet. Not at what the program remembered at the
start of the pass.

## Base operations

This part is for someone embedding the protocol in their own editor.
The names below are actions, not `lrs` functions. On the node, Squish
and JAM do the same thing. The message number does not leave the machine.

```
identity(message) -> msgid[<=200], date, or "no msgid"
find(msgid, date) -> found(local_id)
                   | packed(survivor_local_id, survivor_msgid, survivor_date)
                   | not_yet
                   | error
read_cursor()     -> local_id, mtime
                   | before_first
                   | busy
                   | gone
                   | error
write_cursor(local_id | before_first)
                   -> ok
                   | busy
                   | no_user
                   | gone
                   | error
```

`identity` follows [What a cursor is](#what-a-cursor-is) so both sides
name the same message. `find` searches live messages only.

`read_cursor` returns the time the cursor was last moved when the base
has one. `mtime` is an input to `updated` only when this read shows a
new MSGID or local id.

`write_cursor` changes the lastread and nothing else. `busy` and
`no_user` park the remote cursor and return success for the sync as a
whole. `error` aborts the import and leaves the snapshot in inbound.
`gone` means this echo is no longer subscribed.

The client also needs, outside the base: the peer address, its own
address, the set of subscribed tags, inbound and outbound directories,
and its own memory from [Local store of the instance](#local-store-of-the-instance).

## What a third-party client does not take from the node

Squish and JAM files, message numbers, high-water, the user CRC: that
is the node's business. It does not walk its echo directory. An
`lrs.state` file is not required either. Memory can live anywhere, as
long as the fields are the same as in [Local store of the instance](#local-store-of-the-instance).

In a dispute the editor behaves as the point, not as the node. That is
step 3 in [Who wins](#who-wins). Generation 0 is not a reading place. It
is a question, or the line "the echo is still here". A cursor just
written down from the other end is not our own reading. Ask the node
once and wait for the answer, not on every pass. While an echo stays
subscribed, do not zero its generation.

## Short list for a third-party client

To exchange with the node, a client needs the following.

Its own address and the node's. A way to read and write an `"lrp": 2`
snapshot, and to skip fields it does not know. Its own files named
`c0000001.lrs` and on around to `9999999`, one file in outbound. From
the node, accept `sNNNNNNN.lrs` when `"from"` is the node.

Remember the fields in the table above for each echo. Take inbound
apart first, then send one file. In a dispute, be the point, not the
node.

Name every echo in every file. Dropped an echo, and still send the
file. On a new echo, ask once and write the node's answer before
sending your own reading.

The time in the file is when you read, in UTC seconds. Took the other
end's cursor: write it and do not grow your own generation. Kept yours:
send it again.

The message is not here yet, or the base is busy: wait. The message is
older than the whole base: sit on the oldest one left, and remember
that one. Take the MSGID as the same bytes the node does, no longer
than 200.
