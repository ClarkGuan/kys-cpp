/*
	BASSMIDI synth
	Copyright (c) 2011-2016 Un4seen Developments Ltd.
*/

#include <Carbon/Carbon.h>
#include <stdio.h>
#include <math.h>
#include "bass.h"
#include "bassmidi.h"

WindowPtr win;
EventLoopTimerRef timer;

DWORD input;		// MIDI input device
HSTREAM stream;		// output stream
HSOUNDFONT font;	// soundfont
DWORD preset=0;		// current preset
BOOL drums=0;		// drums enabled?
BOOL chans16=0;		// 16 MIDI channels?

const DWORD fxtype[5]={BASS_FX_DX8_REVERB,BASS_FX_DX8_ECHO,BASS_FX_DX8_CHORUS,BASS_FX_DX8_FLANGER,BASS_FX_DX8_DISTORTION};
HFX fx[5]={0};		// effect handles

#define KEYS 20
const CGKeyCode keys[KEYS]={
	kVK_ANSI_Q,kVK_ANSI_2,kVK_ANSI_W,kVK_ANSI_3,kVK_ANSI_E,kVK_ANSI_R,kVK_ANSI_5,kVK_ANSI_T,kVK_ANSI_6,kVK_ANSI_Y,kVK_ANSI_7,kVK_ANSI_U,
	kVK_ANSI_I,kVK_ANSI_9,kVK_ANSI_O,kVK_ANSI_0,kVK_ANSI_P,kVK_ANSI_LeftBracket,kVK_ANSI_Equal,kVK_ANSI_RightBracket
};

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

	// HACKY: wake up event loop to update display	
	ProcessSerialNumber psn={0,kCurrentProcess};
	WakeUpProcess(&psn);
}

void PostCustomEvent(DWORD id, void *data, DWORD size)
{
	EventRef e;
	CreateEvent(NULL,'blah','blah',0,0,&e);
	SetEventParameter(e,'evid',0,sizeof(id),&id);
	SetEventParameter(e,'data',0,size,data);
	PostEventToQueue(GetMainEventQueue(),e,kEventPriorityHigh);
	ReleaseEvent(e);
}

// MIDI input function
void CALLBACK MidiInProc(DWORD handle, double time, const BYTE *buffer, DWORD length, void *user)
{
	if (chans16) // using 16 channels
		BASS_MIDI_StreamEvents(stream,BASS_MIDI_EVENTS_RAW,buffer,length); // send MIDI data to the MIDI stream
	else
		BASS_MIDI_StreamEvents(stream,(BASS_MIDI_EVENTS_RAW+17)|BASS_MIDI_EVENTS_SYNC,buffer,length); // send MIDI data to channel 17 in the MIDI stream
	PostCustomEvent('acti',0,0);
}

// program/preset event sync function
void CALLBACK ProgramEventSync(HSYNC handle, DWORD channel, DWORD data, void *user)
{
	preset=LOWORD(data);
	BASS_MIDI_FontCompact(0); // unload unused samples
	PostCustomEvent('pres',0,0);
}

void UpdatePresetList()
{
	int a;
	ControlRef cref=GetControl(42);
	MenuRef menu=GetControlPopupMenuHandle(cref);
	DeleteMenuItems(menu,1,128);
	for (a=0;a<128;a++) {
		char text[60];
		const char *name=BASS_MIDI_FontGetPreset(font,a,drums?128:0); // get preset name
		snprintf(text,sizeof(text),"%03d: %s",a,name?name:"");
		CFStringRef cs=CFStringCreateWithCString(0,text,0);
		AppendMenuItemTextWithCFString(menu,cs,0,0,0);
		CFRelease(cs);
	}
	SetControl32BitMaximum(cref,128);
	SetControl32BitValue(cref,preset+1);
	DrawOneControl(cref);
}

pascal OSStatus DeviceEventHandler(EventHandlerCallRef inHandlerRef, EventRef inEvent, void *inUserData)
{
	BASS_MIDI_InFree(input); // free current input device
	input=GetControl32BitValue(inUserData)-1; // get new input device selection
	if (BASS_MIDI_InInit(input,MidiInProc,0)) // successfully initialized...
		BASS_MIDI_InStart(input); // start it
	else
		Error("Can't initialize MIDI device");
	return noErr;
}

pascal OSStatus ChansEventHandler(EventHandlerCallRef inHandlerRef, EventRef inEvent, void *inUserData)
{
	chans16=GetControl32BitValue(inUserData)-1; // MIDI input channels
	return noErr;
}

pascal OSStatus ResetEventHandler(EventHandlerCallRef inHandlerRef, EventRef inEvent, void *inUserData)
{
	BASS_MIDI_StreamEvent(stream,0,MIDI_EVENT_SYSTEM,MIDI_SYSTEM_GS); // send system reset event
	if (drums) BASS_MIDI_StreamEvent(stream,16,MIDI_EVENT_DRUMS,drums); // send drum switch event
	BASS_MIDI_StreamEvent(stream,16,MIDI_EVENT_PROGRAM,preset); // send program/preset event
	return noErr;
}

pascal OSStatus PresetEventHandler(EventHandlerCallRef inHandlerRef, EventRef inEvent, void *inUserData)
{
	preset=GetControl32BitValue(inUserData)-1; // get the selection
	BASS_MIDI_StreamEvent(stream,16,MIDI_EVENT_PROGRAM,preset); // send program/preset event
	BASS_MIDI_FontCompact(0); // unload unused samples
	return noErr;
}

pascal OSStatus DrumsEventHandler(EventHandlerCallRef inHandlerRef, EventRef inEvent, void *inUserData)
{
	drums=GetControl32BitValue(inUserData);
	BASS_MIDI_StreamEvent(stream,16,MIDI_EVENT_DRUMS,drums); // send drum switch event
	preset=BASS_MIDI_StreamGetEvent(stream,16,MIDI_EVENT_PROGRAM); // preset is reset in drum switch
	UpdatePresetList();
	BASS_MIDI_FontCompact(0); // unload unused samples
}

pascal OSStatus SincEventHandler(EventHandlerCallRef inHandlerRef, EventRef inEvent, void *inUserData)
{
	BASS_ChannelSetAttribute(stream,BASS_ATTRIB_MIDI_SRC,GetControl32BitValue(inUserData)-1); // set interpolation points
	return noErr;
}

pascal OSStatus FXEventHandler(EventHandlerCallRef inHandlerRef, EventRef inEvent, void *inUserData)
{ // toggle effects
	DWORD on=GetControl32BitValue(inUserData);
	ControlID cid;
	GetControlID(inUserData,&cid);
	int n=cid.id-35;
	if (!on) {
		BASS_ChannelRemoveFX(stream,fx[n]);
		fx[n]=0;
	} else
		fx[n]=BASS_ChannelSetFX(stream,fxtype[n],n);
	return noErr;
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
					BASS_MIDI_StreamSetFonts(stream,&sf,1); // apply to current stream too
					BASS_MIDI_FontFree(font); // free old soundfont
					font=newfont;
					{
						BASS_MIDI_FONTINFO i;
						BASS_MIDI_FontGetInfo(font,&i);
						CFStringRef cs=CFStringCreateWithCString(0,i.name?i.name:strrchr(file,'/')+1,kCFStringEncodingWindowsLatin1);
						SetControlTitleWithCFString(inUserData,cs);
						CFRelease(cs);
						if (i.presets==1) { // only 1 preset, auto-select it...
							DWORD p;
							BASS_MIDI_FontGetPresets(font,&p);
							drums=(HIWORD(p)==128); // bank 128 = drums
							preset=LOWORD(p);
							SetControl32BitValue(GetControl(44),0);
							BASS_MIDI_StreamEvent(stream,16,MIDI_EVENT_DRUMS,drums); // send drum switch event
							BASS_MIDI_StreamEvent(stream,16,MIDI_EVENT_PROGRAM,preset); // send program/preset event
							DisableControl(GetControl(42));
							DisableControl(GetControl(44));
						} else {
							EnableControl(GetControl(42));
							EnableControl(GetControl(44));
						}
					}
					UpdatePresetList();
				}
			}
			NavDisposeReply(&r);
		}
	}
	NavDialogDispose(fileDialog);
    return noErr;
}

static OSStatus KeyEventHandler(EventHandlerCallRef inCaller, EventRef inEvent, void *inUserData)
{
	UInt32 kc=0;
	GetEventParameter(inEvent, kEventParamKeyCode, typeUInt32, NULL, sizeof(UInt32), NULL, &kc);
	int key;
	for (key=0;key<KEYS;key++)
		if (kc==keys[key]) {
			bool down=GetEventKind(inEvent)==kEventRawKeyDown;
			BASS_MIDI_StreamEvent(stream,16,MIDI_EVENT_NOTE,MAKEWORD((drums?36:60)+key,down?100:0)); // send note on/off event
			break;
		}
	return 0;
}

pascal OSStatus CustomEventHandler(EventHandlerCallRef inHandlerRef, EventRef inEvent, void *inUserData)
{
	DWORD id=0;
	GetEventParameter(inEvent,'evid',0,NULL,sizeof(id),NULL,&id);
	switch (id) {
		case 'pres':
			SetControl32BitValue(GetControl(42),preset+1); // update the preset selector
			break;
		case 'acti':
			{
				ControlFontStyleRec fsr={kControlUseForeColorMask|kControlUseBackColorMask|kControlUseJustMask,0,0,0,0,teCenter,{0},{0,0xffff,0}};
				SetControlFontStyle(GetControl(11),&fsr);
				DrawOneControl(GetControl(11));
				SetEventLoopTimerNextFireTime(timer,0.1);
			}
			break;

	}
	return noErr;
}

pascal void TimerProc(EventLoopTimerRef inTimer, void *inUserData)
{
	ControlFontStyleRec fsr={kControlUseForeColorMask|kControlUseBackColorMask|kControlUseJustMask,0,0,0,0,teCenter,{0},{0xffff,0xffff,0xffff}};
	SetControlFontStyle(GetControl(11),&fsr);
	DrawOneControl(GetControl(11));
}

int main(int argc, char* argv[])
{
	IBNibRef 		nibRef;
	OSStatus		err;

	// check the correct BASS was loaded
	if (HIWORD(BASS_GetVersion())!=BASSVERSION) {
		Error("An incorrect version of BASS was loaded");
		return 0;
	}

	// initialize default output device
	if (!BASS_Init(-1,44100,0,0,NULL))
		Error("Can't initialize output device");

	// Create Window and stuff
	err = CreateNibReference(CFSTR("synth"), &nibRef);
	if (err) return err;
	err = CreateWindowFromNib(nibRef, CFSTR("Window"), &win);
	if (err) return err;
	DisposeNibReference(nibRef);

	stream=BASS_MIDI_StreamCreate(17,BASS_SAMPLE_FLOAT,1); // create the MIDI stream (16 MIDI channels for device input + 1 for keyboard input)
	BASS_ChannelSetAttribute(stream,BASS_ATTRIB_NOBUFFER,1); // no buffering for minimum latency
	BASS_ChannelSetAttribute(stream,BASS_ATTRIB_MIDI_CPU,75); // limit CPU usage to 75% (also enables async sample loading)
	BASS_ChannelSetSync(stream,BASS_SYNC_MIDI_EVENT|BASS_SYNC_MIXTIME,MIDI_EVENT_PROGRAM,ProgramEventSync,0); // catch program/preset changes
	BASS_MIDI_StreamEvent(stream,0,MIDI_EVENT_SYSTEM,MIDI_SYSTEM_GS); // send GS system reset event
	BASS_ChannelPlay(stream,0); // start it
	{ // enumerate available input devices
		ControlRef cref=GetControl(10);
		MenuRef menu=GetControlPopupMenuHandle(cref);
		BASS_MIDI_DEVICEINFO di;
		int dev;
		for (dev=0;BASS_MIDI_InGetDeviceInfo(dev,&di);dev++) {
			CFStringRef cs=CFStringCreateWithCString(0,di.name,0);
			AppendMenuItemTextWithCFString(menu,cs,0,0,0);
			CFRelease(cs);
		}
		if (dev) { // got sone, try to initialize one
			int a;
			SetControl32BitMaximum(cref,dev);
			for (a=0;a<dev;a++) {
				if (BASS_MIDI_InInit(a,MidiInProc,0)) { // succeeded, start it
					input=a;
					BASS_MIDI_InStart(input);
					SetControl32BitValue(cref,input+1);
					break;
				}
			}
			if (a==dev) Error("Can't initialize MIDI device");
		} else {
			AppendMenuItemTextWithCFString(menu,CFSTR("no devices"),0,0,0);
			SetControl32BitMaximum(cref,1);
			SetControl32BitValue(cref,1);
			DisableControl(cref);
			DisableControl(GetControl(12));
		}
	}
	UpdatePresetList();
	// load optional plugins for packed soundfonts (others may be used too)
	BASS_PluginLoad("libbassflac.dylib",0);
	BASS_PluginLoad("libbasswv.dylib",0);

	SetupControlHandler(15,kEventControlHit,ResetEventHandler);
	SetupControlHandler(35,kEventControlHit,FXEventHandler);
	SetupControlHandler(36,kEventControlHit,FXEventHandler);
	SetupControlHandler(37,kEventControlHit,FXEventHandler);
	SetupControlHandler(38,kEventControlHit,FXEventHandler);
	SetupControlHandler(39,kEventControlHit,FXEventHandler);
	SetupControlHandler(40,kEventControlHit,OpenFontEventHandler);
	SetupControlHandler(44,kEventControlHit,DrumsEventHandler);
	SetupControlHandler(10,kEventControlValueFieldChanged,DeviceEventHandler);
	SetupControlHandler(12,kEventControlValueFieldChanged,ChansEventHandler);
	SetupControlHandler(42,kEventControlValueFieldChanged,PresetEventHandler);
	SetupControlHandler(50,kEventControlValueFieldChanged,SincEventHandler);

	EventTypeSpec events[]={
		{kEventClassKeyboard,kEventRawKeyUp},
		{kEventClassKeyboard,kEventRawKeyDown},
	};
	EventHandlerRef eh;
    InstallApplicationEventHandler(KeyEventHandler,GetEventTypeCount(events),events,0,&eh);
	{
		EventTypeSpec etype={'blah','blah'};
		InstallApplicationEventHandler(NewEventHandlerUPP(CustomEventHandler),1,&etype,NULL,NULL);
	}

	ControlFontStyleRec fsr={kControlUseFontMask|kControlUseSizeMask|kControlUseJustMask,ATSFontFamilyFindFromName(CFSTR("Courier"),0),20,0,0,teCenter};
	SetControlFontStyle(GetControl(20),&fsr);

	InstallEventLoopTimer(GetCurrentEventLoop(),kEventDurationNoWait,0,NewEventLoopTimerUPP(TimerProc),0,&timer);

	ShowWindow(win);
	RunApplicationEventLoop();

	BASS_MIDI_InFree(input);
	BASS_Free();
	BASS_PluginFree(0);

	return 0; 
}
