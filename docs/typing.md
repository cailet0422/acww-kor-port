<p align="center"><b>English</b> · <a href="typing.ko.md">한국어</a></p>

# Typing with the device's keyboard

The game has a soft keyboard of its own on the lower screen: for the player's and the town's
names, catchphrases, letters, the bulletin board and the chat. With "type with the phone's
keyboard" on (`type_keyboard`, on by default on Android, a switch under Settings > Controls;
`--type-keyboard` elsewhere), the device's keyboard types there instead, and the game's
stays usable beside it.

## How a character gets into the game

The four screens that use the keyboard (the name and text entry, the chat, the board's
writing and the letter's: overlays 126, 111, 112 and 122 over the keyboard's own, 95) work
alike. When the pen comes down on a key, the screen asks the keyboard for the key's code
(`func_ov095_0229c448` in the decomp: the character on the key in the page shown, Unicode, or
a number from 0x100 for the keys around the pages) and acts on the code. A character is put
into the text; Hangul comes as its letters (U+3131 and on), which the keyboard puts together
into syllables itself, as a phone's two-set keyboard does; 0x100 deletes backwards.

So a character is typed by touching a key that every page has (the space bar) and answering
the question for its code with the character wanted (`port/src/game_keyboard.c`). Whatever
page is shown, and with everything the screen does about a typed character done by the game
itself: the text's length and width limits, the composing of Hangul, the sounds. Nothing is
written into the game's text from outside.

- The hooks are three addresses of overlay 95, taken only while the code there is the
  keyboard's (other overlays are loaded at the same addresses at other times; a number made
  of the functions' first bytes tells).
- The keyboard's own turn, which every screen with the keyboard gives it once a turn of the
  game (`func_ov095_0229bf7c`), says that such a screen is up: the device's keyboard is then
  asked for text, and put away when the screen is gone.
- A key takes the pen down until the game has asked for the code, and five frames up: the
  game takes no key for a turn after one, and sees the pen gone only after some frames.
  That is some six keys a second, a Hangul syllable being two or three.
- The first touch after the game's buttons were used only turns the game to the pen: it is
  tried again. If a touch is not taken three times, the game is not taking keys (a question
  is up), and what was waiting is given up.
- A character the text has no room for is refused by the game (a flag on the keyboard,
  `func_ov095_022999a8` with 0x40 or 0x80, and the game's own buzz). What was typed past it
  is let go of, and the device's keyboard begins anew.
- The characters the game's keys can give are read from the game's tables in memory when the
  keyboard is first seen; a character that has no key is left out. Letters, digits and marks
  are typed as the game's own full-width ones.

## From a phone's text to the game's keys

A phone's keyboard does not say which keys were pressed. It says what its text is: while a
syllable is typed, the syllable so far is taken back and given anew at every letter (ㅎ, 하,
한), and SDL passes that on as a backspace and a piece of text. Typed into the game like
that, every letter would cost the whole syllable again.

`port/src/typing.c` keeps the two side by side: the text the phone has, the keys the game has
been given, and, from those, the next key that brings the game's text nearer the phone's: one
letter more, or one deletion. For that it follows what the game does with each key exactly
as the game does it (`decomp/src/ov095/soft_keyboard_hangul.c`, `func_ov095_0229b784.c`,
`func_ov095_0229b3a0.c`): which letters it joins into a syllable, when a final consonant goes
over to the next syllable (한 and ㅏ make 하나), which syllables it knows (2355 of them, read
from the game), and what a deletion removes (a letter of the syllable being typed, else a
whole character). A syllable grows by one key for each letter typed.

Where the game's characters would come apart from the phone's with the same keys, it is put
right: a syllable the phone has ended (한, then ㅏ on its own) is ended in the game by a space
that is deleted again; a letter that belongs on a character the game has stopped adding to
(after a deletion) has that character typed anew. A syllable the game does not know stays
as the game makes it of the same keys (닭 as 달ㄱ in a game without it).

The player's own pen on the game's screen, and the game's own keys, may change the text by
other means: the phone's keyboard then begins anew, and its backspace deletes in the game
what is there.

`acww-typing-test` plays a phone's Korean keyboard against the game's keyboard written out a
second time from the decomp: twelve letters take twelve keys; a line given at once; characters
without a key; a game that knows fewer syllables; 20 000 steps of typing, taking back and
ending syllables by chance, after each of which the game's text is the phone's.

## Enter

Where the text is one line, Enter on the device's keyboard ends the typing as the screen's
own button does: on a name's (and any other text entry's) screen, Decide; in the chat, the
arrow that sends the line. On a letter and on the board, where the text has lines, it is the
end of a line, as before.

Which screen is over the keyboard is told by the code at the function each has for acting on
a key's code (`func_ov126_022a03dc` of the entry, `func_ov111_0229f730` of the chat): the
screens are overlays at the same addresses. The button is touched with the pen, as a finger
would touch it (not by calling what it does), so that the game's own rules hold: Decide does
nothing while the name is empty, as it does nothing under a finger.

- It is touched only once everything typed before is in the game. Then, first, a key that
  does nothing is typed (the code 0x101, one the screens ignore): when the game takes it, the
  game is taking keys and is turned to the pen, and the touch that follows is the button's.
  If it is not taken three times, Enter is given up.
- What is typed after Enter and before the touch is typed after it: the next line of a chat.
  After a name's Decide it is let go of.
- Android's keyboards give Enter as a key, or as the end of a line in text, which SDL passes
  on as a key as well: within a frame it is one Enter.

## The screen being typed on

A keyboard on the screen covers the lower part of the window, where the DS's lower screen
is, and that is the screen being typed on. While it is covered, the lower screen is shown
where the upper one is otherwise, at the window's top (smaller, if the room above the
keyboard is less than the screen's height), and the upper screen is out of sight above it.
The pen is where the lower screen is shown, so the game's own buttons there (the pages,
Decide) are touched as they are seen. `MainActivity.keyboardCover` says how much of the
picture the keyboard covers.

If the phone's keyboard is put away while the game's is up, a touch on the upper part of the
lower screen, where the text is, brings it back.

## Checked, and not

- On this machine, with the ROM: the name entry of a new game typed through the whole path
  (`--type FRAME:TEXT` gives the text as if typed), Hangul, a space, Latin letters, digits and
  deletions, on whichever page the game's keyboard showed; the text's limit refused and the
  rest let go of; the window's layout with a part covered (`PORT_TYPE_COVER=PIXELS`).
- Also on this machine: a line of chat typed and sent with Enter, and a second one after it;
  a name ended with Enter; Enter on an empty name, which does nothing (`\n` in `--type` is
  Enter, as the key a keyboard gives).
- Not on this machine: the board and the letter (they need a town further on than the one
  there is here). They ask the keyboard the same questions at the same addresses, by the
  decomp's sources.
- Not on a phone: what Android's keyboards send, through SDL, is taken from SDL's source
  (`SDLInputConnection.updateText`), and the keyboard's height from the system's insets.
