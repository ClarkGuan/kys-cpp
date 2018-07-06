/*
	BASSMIDI test player
	Copyright (c) 2006-2017 Un4seen Developments Ltd.
*/

#include <Carbon/Carbon.h>
#include <math.h>
#include "bass.h"
#include "bassmidi.h"

WindowPtr win;

HSTREAM chan;		// channel handle
HSOUNDFONT font;	// soundfont

float speed=1;	// tempo adjustment

char lyrics[1000]; // lyrics buffer

// display error messages
void Error(const char *es)
{
	short i;
	char mes[200];
	sprintf(mes,"%s\n(error code: %d)",es,BASS_ErrorGetCode());
	CFStringRef ces=CFStringCreateWithCString(0,mes,0);
	DialogRef alert;
	CreateStandardAlert(0,CFSTR("Error"),ces,NULL,&alert);
	RunStandardAlert(alert,NULL,&i);
	CFRelease(ces);
}

ControlRef GetControl(int id)
{
	ControlRef cref;
	ControlID cid={0,id};
	GetControlByID(win,&cid,&cref);
	return cref;
}

void SetupControlHandler(int id, DWORD event, EventHandlerProcPtr proc)
{
	EventTypeSpec etype={kEventClassControl,event};
	ControlRef cref=GetControl(id);
	InstallControlEventHandler(cref,NewEventHandlerUPP(proc),1,&etype,cref,NULL);
}

void SetStaticText(int id, const char *text)
{
	ControlRef cref=GetControl(id);
	SetControlData(cref,kControlNoPart,kControlStaticTextTextTag,strlen(text),text);
	DrawOneControl(cref);
}

void PostLyricRefreshEvent()
{
	EventRef e;
	CreateEvent(NULL,'blah','blah',0,0,&e);
	PostEventToQueue(GetMainEventQueue(),e,kEventPriorityHigh);
	ReleaseEvent(e);
}

void CALLBACK LyricSync(HSYNC handle, DWORD channel, DWORD data, void *user)
{
	BASS_MIDI_MARK mark;
	const char *text;
	char *p;
	int lines;
	BASS_MIDI_StreamGetMark(channel,(DWORD)user,data,&mark); // get the lyric/text
	text=mark.text;
	if (text[0]=='@') return; // skip info
	if (text[0]=='\\') { // clear display
		p=lyrics;
		text++;
	} else {
		p=lyrics+strlen(lyrics);
		if (text[0]=='/') { // new line
			*p++='\n';
			text++;
		}
	}
	sprintf(p,"%.*s",lyrics+sizeof(lyrics)-p-1,text); // add the text to the lyrics buffer
	for (lines=1,p=lyrics;p=strchr(p,'\n');lines++,p++) ; // count lines
	if (lines>3) { // remove old lines so that new lines fit in display...
		int a;
		for (a=0,p=lyrics;a<lines-3;a++) p=strchr(p,'\n')+1;
		strcpy(lyrics,p);
	}
	PostLyricRefreshEvent();
}

void CALLBACK EndSync(HSYNC handle, DWORD channel, DWORD data, void *user)
{
	lyrics[0]=0; // clear lyrics
	PostLyricRefreshEvent();
}

// look for a marker (eg. loop points)
BOOL FindMarker(HSTREAM handle, const char *text, BASS_MIDI_MARK *mark)
{
	int a;
	for (a=0;BASS_MIDI_StreamGetMark(handle,BASS_MIDI_MARK_MARKER,a,mark);a++) {
		if (!strcasecmp(mark->text,text)) return TRUE; // found it
	}
	return FALSE;
}

void CALLBACK LoopSync(HSYNC handle, DWORD channel, DWORD data, void *user)
{
	BASS_MIDI_MARK mark;
	if (FindMarker(channel,"loopstart",&mark)) // found a loop start point
		BASS_ChannelSetPosition(channel,mark.pos,BASS_POS_BYTE|BASS_MIDI_DECAYSEEK); // rewind to it (and let old notes decay)
	else
		BASS_ChannelSetPosition(channel,0,BASS_POS_BYTE|BASS_MIDI_DECAYSEEK); // else rewind to the beginning instead
}

pascal OSStatus OpenEventHandler(EventHandlerCallRef inHandlerRef, EventRef inEvent, void *inUserData)
{
	NavDialogRef fileDialog;
	NavDialogCreationOptions fo;
	NavGetDefaultDialogCreationOptions(&fo);
	fo.optionFlags=0;
	fo.parentWindow=win;
	NavCreateChooseFileDialog(&fo,NULL,NULL,NULL,NULL,NULL,&fileDialog);
// if someone wants to somehow get the file selector to filter like in the Windows example, that'd be nice ;)
	if (!NavDialogRun(fileDialog)) {
		NavReplyRecord r;
		if (!NavDialogGetReply(fileDialog,&r)) {
			AEKeyword k;
			FSRef fr;
			if (!AEGetNthPtr(&r.selection,1,typeFSRef,&k,NULL,&fr,sizeof(fr),NULL)) {
				char file[256];
				FSRefMakePath(&fr,(BYTE*)file,sizeof(file));
				BASS_StreamFree(chan); // free old stream before opening new
				SetStaticText(30,""); // clear lyrics display
				if (!(chan=BASS_MIDI_StreamCreateFile(FALSE,file,0,0,BASS_SAMPLE_FLOAT|BASS_SAMPLE_LOOP|BASS_MIDI_DECAYSEEK|(GetControl32BitValue(GetControl(20))?0:BASS_MIDI_NOFX),1))) {
					// it ain't a MIDI
					SetControlTitleWithCFString(inUserData,CFSTR("click here to open a file..."));
					SetStaticText(11,"");
					SetControl32BitMaximum(GetControl(21),0);
					Error("Can't play the file");
				} else {
					CFStringRef cs=CFStringCreateWithCString(0,file,kCFStringEncodingUTF8);
					SetControlTitleWithCFString(inUserData,cs);
					CFRelease(cs);
					{ // set the title (track name of first track)
						BASS_MIDI_MARK mark;
						if (BASS_MIDI_StreamGetMark(chan,BASS_MIDI_MARK_TRACK,0,&mark) && !mark.track)
							SetStaticText(11,mark.text);
						else
							SetStaticText(11,"");
					}
					// update pos scroller range (using tick length)
					SetControl32BitMaximum(GetControl(21),BASS_ChannelGetLength(chan,BASS_POS_MIDI_TICK)/120);
					{ // set looping syncs
						BASS_MIDI_MARK mark;
						if (FindMarker(chan,"loopend",&mark)) // found a loop end point
							BASS_ChannelSetSync(chan,BASS_SYNC_POS|BASS_SYNC_MIXTIME,mark.pos,LoopSync,0); // set a sync there
						BASS_ChannelSetSync(chan,BASS_SYNC_END|BASS_SYNC_MIXTIME,0,LoopSync,0); // set one at the end too (eg. in case of seeking past the loop point)
					}
					{ // clear lyrics buffer and set lyrics syncs
						BASS_MIDI_MARK mark;
						lyrics[0]=0;
						if (BASS_MIDI_StreamGetMark(chan,BASS_MIDI_MARK_LYRIC,0,&mark)) // got lyrics
							BASS_ChannelSetSync(chan,BASS_SYNC_MIDI_MARK,BASS_MIDI_MARK_LYRIC,LyricSync,(void*)BASS_MIDI_MARK_LYRIC);
						else if (BASS_MIDI_StreamGetMark(chan,BASS_MIDI_MARK_TEXT,20,&mark)) // got text instead (over 20 of them)
							BASS_ChannelSetSync(chan,BASS_SYNC_MIDI_MARK,BASS_MIDI_MARK_TEXT,LyricSync,(void*)BASS_MIDI_MARK_TEXT);
						BASS_ChannelSetSync(chan,BASS_SYNC_END,0,EndSync,0);
					}
					BASS_MIDI_StreamEvent(chan,0,MIDI_EVENT_SPEED,speed*10000); // apply tempo adjustment
					{ // get default soundfont in case of matching soundfont being used
						BASS_MIDI_FONT sf;
						BASS_MIDI_StreamGetFonts(chan,&sf,1);
						font=sf.font;
					}
					// limit CPU usage to 70% and start playing
					BASS_ChannelSetAttribute(chan,BASS_ATTRIB_MIDI_CPU,70);
					BASS_ChannelPlay(chan,FALSE);
				}
			}
			NavDisposeReply(&r);
		}
	}
	NavDialogDispose(fileDialog);
    return noErr;
}

pascal void PosEventHandler(ControlHandle control, SInt16 part)
{
	DWORD p=GetControl32BitValue(control);
	BASS_ChannelSetPosition(chan,p*120,BASS_POS_MIDI_TICK);
	// clear lyrics
	lyrics[0]=0;
	SetStaticText(30,lyrics);
}

pascal OSStatus FXEventHandler(EventHandlerCallRef inHandlerRef, EventRef inEvent, void *inUserData)
{ // toggle FX processing
	if (GetControl32BitValue(inUserData))
		BASS_ChannelFlags(chan,0,BASS_MIDI_NOFX); // enable FX
	else
		BASS_ChannelFlags(chan,BASS_MIDI_NOFX,BASS_MIDI_NOFX); // disable FX
	return noErr;
}

pascal void TempoEventHandler(ControlHandle control, SInt16 part)
{
	int p=GetControl32BitValue(control); // get tempo slider pos
	speed=(20+p)/20.f; // up to +/- 50% bpm
	BASS_MIDI_StreamEvent(chan,0,MIDI_EVENT_SPEED,speed*10000); // apply tempo adjustment
}

pascal OSStatus OpenFontEventHandler(EventHandlerCallRef inHandlerRef, EventRef inEvent, void *inUserData)
{
	NavDialogRef fileDialog;
	NavDialogCreationOptions fo;
	NavGetDefaultDialogCreationOptions(&fo);
	fo.optionFlags=0;
	fo.parentWindow=win;
	NavCreateChooseFileDialog(&fo,NULL,NULL,NULL,NULL,NULL,&fileDialog);
// if someone wants to somehow get the file selector to filter like in the Windows example, that'd be nice ;)
	if (!NavDialogRun(fileDialog)) {
		NavReplyRecord r;
		if (!NavDialogGetReply(fileDialog,&r)) {
			AEKeyword k;
			FSRef fr;
			if (!AEGetNthPtr(&r.selection,1,typeFSRef,&k,NULL,&fr,sizeof(fr),NULL)) {
				char file[256];
				FSRefMakePath(&fr,(BYTE*)file,sizeof(file));
				HSOUNDFONT newfont=BASS_MIDI_FontInit(file,0);
				if (newfont) {
					BASS_MIDI_FONT sf;
					sf.font=newfont;
					sf.preset=-1; // use all presets
					sf.bank=0; // use default bank(s)
					BASS_MIDI_StreamSetFonts(0,&sf,1); // set default soundfont
					BASS_MIDI_StreamSetFonts(chan,&sf,1); // set for current stream too
					BASS_MIDI_FontFree(font); // free old soundfont
					font=newfont;
				}
			}
			NavDisposeReply(&r);
		}
	}
	NavDialogDispose(fileDialog);
    return noErr;
}

pascal void TimerProc(EventLoopTimerRef inTimer, void *inUserData)
{
	if (chan) {
		char text[16];
		DWORD tick=BASS_ChannelGetPosition(chan,BASS_POS_MIDI_TICK); // get position in ticks
		int tempo=BASS_MIDI_StreamGetEvent(chan,0,MIDI_EVENT_TEMPO); // get the file's tempo
		SetControl32BitValue(GetControl(21),tick/120); // update position
		sprintf(text,"%.1f",speed*60000000.0/tempo); // calculate bpm
		SetStaticText(23,text); // display it
	}
	{
		static int updatefont=0;
		if (++updatefont&1) { // only updating font info once a second
			char text[80]="no soundfont";
			BASS_MIDI_FONTINFO i;
			if (BASS_MIDI_FontGetInfo(font,&i))
				snprintf(text,sizeof(text),"name: %s\nloaded: %d / %d",i.name,i.samload,i.samsize);
			SetStaticText(41,text);
		}
	}
}

pascal OSStatus LyricRefreshEventHandler(EventHandlerCallRef inHandlerRef, EventRef inEvent, void *inUserData)
{
	SetStaticText(30,lyrics);
	return noErr;
}

int main(int argc, char* argv[])
{
	IBNibRef nibRef;
	OSStatus err;
    
	// check the correct BASS was loaded
	if (HIWORD(BASS_GetVersion())!=BASSVERSION) {
		Error("An incorrect version of BASS was loaded");
		return 0;
	}

	// initialize default output device
	if (!BASS_Init(-1,44100,0,NULL,NULL)) {
		Error("Can't initialize device");
		return 0;
	}

	// Create Window and stuff
	err = CreateNibReference(CFSTR("miditest"), &nibRef);
	if (err) return err;
	err = CreateWindowFromNib(nibRef, CFSTR("Window"), &win);
	if (err) return err;
	DisposeNibReference(nibRef);

	// load optional plugins for packed soundfonts (others may be used too)
	BASS_PluginLoad("libbassflac.dylib",0);
	BASS_PluginLoad("libbasswv.dylib",0);

	SetupControlHandler(10,kEventControlHit,OpenEventHandler);
	SetupControlHandler(20,kEventControlHit,FXEventHandler);
	SetControlAction(GetControl(21),NewControlActionUPP(PosEventHandler));
	SetControlAction(GetControl(22),NewControlActionUPP(TempoEventHandler));
	SetupControlHandler(40,kEventControlHit,OpenFontEventHandler);

	EventLoopTimerRef timer;
	InstallEventLoopTimer(GetCurrentEventLoop(),kEventDurationNoWait,kEventDurationSecond/2,NewEventLoopTimerUPP(TimerProc),0,&timer);
	{
		EventTypeSpec etype={'blah','blah'};
		InstallApplicationEventHandler(NewEventHandlerUPP(LyricRefreshEventHandler),1,&etype,NULL,NULL);
	}

	ShowWindow(win);
	RunApplicationEventLoop();

	// free the output device and all plugins
	BASS_Free();
	BASS_PluginFree(0);

    return 0; 
}
