#if defined(TARGET_4848S040)

extern "C" {
LV_FONT_DECLARE(lv_font_inter_14);
LV_FONT_DECLARE(lv_font_inter_16);
LV_FONT_DECLARE(lv_font_inter_18);
LV_FONT_DECLARE(lv_font_inter_display_32);
LV_FONT_DECLARE(lv_font_inter_display_48);
}

// Native 480x480 interface.  The networking and parsers remain the proven
// T-Display implementation; this layer only presents that model on the larger
// touch panel.
static lv_obj_t *ui480Pages[6] = {};
static lv_obj_t *uHeader, *uStatusTitle, *uStatus, *uDirection, *uFlowShaft, *uFlowHead, *uSocArc, *uSoc, *uVolt;
static lv_obj_t *uBatteryCells[4] = {};
static lv_obj_t *uSocCard, *uPowerCard, *uCurrentCard, *uMainBottom;
static lv_obj_t *uPower, *uCurrent, *uBottom, *uWarningBar, *uWarningText;
static lv_obj_t *uCellsTitle, *uCellMaxValue, *uCellMinValue, *uDeltaValue, *uTempTitle, *uTempValue;
static lv_obj_t *uCellMin, *uCellMax, *uCellDelta, *uCellTemp;
static lv_obj_t *uBars[96] = {}, *uMinTag, *uMaxTag;
static lv_obj_t *uEventRows[10] = {}, *uEventCards[10] = {}, *uEventCount;
static lv_obj_t *uEventList, *uEventMore, *uEventEmpty;
static lv_obj_t *uContactorState, *uEmuClosing, *uInvClosing, *uContactorWarning;
static lv_obj_t *uBatteryHealth, *uChargeLimits, *uDischargeLimits;
static lv_obj_t *uAdvancedCard, *uHvilMetrics, *uBmsMetrics, *uIsolationMetrics, *uSystemInfo, *uActionToast;
static lv_obj_t *uDtcPage=nullptr, *uDtcList=nullptr, *uDtcSummary=nullptr, *uDtcToast=nullptr;
static lv_obj_t *uDevPage=nullptr, *uDevTitle=nullptr;
static lv_obj_t *uDtcCards[MAX_DISPLAY_DTCS]={}, *uDtcRows[MAX_DISPLAY_DTCS]={};
static bool uDtcVisible=false;
static uint32_t uCriticalPressAt=0;
static int uCriticalAction=-1;
static lv_obj_t *uDualDash=nullptr, *uDualDashCard[3]={}, *uDualDashText[3]={};
static lv_obj_t *uDualCells=nullptr, *uDualCellCard[3]={}, *uDualCellText[3]={}, *uDualCellPlot[3]={};
static lv_obj_t *uWizard = nullptr, *uKeyboard = nullptr;
static lv_obj_t *uHeartbeat = nullptr;
static lv_obj_t *uSsid = nullptr, *uPassword = nullptr, *uHost = nullptr;
static lv_obj_t *uKeyboardTarget = nullptr;
static bool uKeyboardUpper = false;
static bool uKeyboardSymbols = false;
static uint8_t uWizardStep = 0;
static bool uWizardDirectMode = true;
static String uWizardSaveError;
static int8_t uFlowDirection = 0;
static int8_t uPreviousFlowDirection = 0;
static lv_color_t uFlowArrowColor;
static uint8_t uBatteryFlowPhase = 0;
static uint32_t uFlowAnimationAt = 0;
static uint32_t uHeartbeatAt = 0;
static uint8_t uHeartbeatLevel = 0;
static bool uHeartbeatBright = true;
static void uWizardRender();
static void uWizardOpen(lv_event_t*);

static lv_color_t U_BG() { return lv_color_hex(0x02070D); }
static lv_color_t U_CARD() { return lv_color_hex(0x071522); }
static lv_color_t U_LINE() { return lv_color_hex(0x344D67); }
static lv_color_t U_BLUE() { return lv_color_hex(0x16C9F2); }
static lv_color_t U_GREEN() { return lv_color_hex(0x63E21A); }
static lv_color_t U_RED() { return lv_color_hex(0xFF332B); }
static lv_color_t U_AMBER() { return lv_color_hex(0xFFB000); }
static lv_color_t U_SOFT_AMBER() { return lv_color_hex(0xE89A3C); }
static lv_color_t U_MUTED() { return lv_color_hex(0x8DA2B7); }

static lv_coord_t uTextWidth(const String &text,const lv_font_t *font) {
  return lv_txt_get_width(text.c_str(),text.length(),font,-2,LV_TEXT_FLAG_NONE);
}

static void uRenderBatteryFlow() {
  for(int i=0;i<4;i++) {
    if(!uBatteryCells[i]) continue;
    if(uFlowDirection==0) {
      lv_obj_set_style_bg_color(uBatteryCells[i],U_GREEN(),0);
      lv_obj_set_style_bg_opa(uBatteryCells[i],LV_OPA_70,0);
      continue;
    }
    // A filling/emptying sequence is easier to read than a travelling glint:
    // charge lights 1 -> 4 blocks, discharge turns 4 -> 1 blocks off.
    int litCount=uFlowDirection<0?uBatteryFlowPhase+1:4-uBatteryFlowPhase;
    lv_obj_set_style_bg_color(uBatteryCells[i],uFlowArrowColor,0);
    lv_obj_set_style_bg_opa(uBatteryCells[i],i<litCount?LV_OPA_COVER:LV_OPA_20,0);
  }
}

// A real vector arrow avoids font-dependent chevrons. It remains static and
// only changes direction; the animated energy flow lives inside the battery.
static void uRenderFlowArrow() {
  static lv_point_t shaftRight[]={{0,9},{27,9}};
  static lv_point_t headRight[]={{17,0},{27,9},{17,18}};
  static lv_point_t shaftLeft[]={{27,9},{0,9}};
  static lv_point_t headLeft[]={{10,0},{0,9},{10,18}};
  if(!uFlowShaft || !uFlowHead) return;
  if(uFlowDirection==0) {
    lv_obj_add_flag(uFlowShaft,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(uFlowHead,LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_clear_flag(uFlowShaft,LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(uFlowHead,LV_OBJ_FLAG_HIDDEN);
  lv_point_t *shaft=uFlowDirection>0?shaftRight:shaftLeft;
  lv_point_t *head=uFlowDirection>0?headRight:headLeft;
  lv_line_set_points(uFlowShaft,shaft,2);
  lv_line_set_points(uFlowHead,head,3);
  lv_obj_set_pos(uFlowShaft,87,18);
  lv_obj_set_pos(uFlowHead,87,18);
  lv_obj_set_style_line_color(uFlowShaft,uFlowArrowColor,0);
  lv_obj_set_style_line_color(uFlowHead,uFlowArrowColor,0);
  lv_obj_set_style_line_opa(uFlowShaft,LV_OPA_COVER,0);
  lv_obj_set_style_line_opa(uFlowHead,LV_OPA_COVER,0);
}

static void uSetFlowArrow(int8_t direction,lv_color_t color) {
  if(direction!=uPreviousFlowDirection) {
    uBatteryFlowPhase=0;
    uPreviousFlowDirection=direction;
  }
  uFlowDirection=direction;
  uFlowArrowColor=color;
  uRenderFlowArrow();
  uRenderBatteryFlow();
}

static void ui480Animate(uint32_t now) {
  if(uHeartbeat) {
    bool wizardVisible=uWizard && !lv_obj_has_flag(uWizard,LV_OBJ_FLAG_HIDDEN);
    bool mainDashboardOnly=page==0 && !wizardVisible;
    if(mainDashboardOnly) lv_obj_add_flag(uHeartbeat,LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(uHeartbeat,LV_OBJ_FLAG_HIDDEN);
    uint32_t heartbeatInterval=uHeartbeatLevel==0?650:350;
    if(!mainDashboardOnly && now-uHeartbeatAt>=heartbeatInterval) {
      uHeartbeatAt=now;
      uHeartbeatBright=!uHeartbeatBright;
      lv_color_t color=uHeartbeatLevel>=2?U_RED():(uHeartbeatLevel==1?U_AMBER():U_GREEN());
      lv_obj_set_style_bg_color(uHeartbeat,color,0);
      lv_obj_set_style_shadow_color(uHeartbeat,color,0);
      lv_obj_set_style_bg_opa(uHeartbeat,uHeartbeatBright?LV_OPA_COVER:LV_OPA_30,0);
      lv_obj_set_style_shadow_opa(uHeartbeat,uHeartbeatBright?LV_OPA_60:LV_OPA_10,0);
      lv_obj_move_foreground(uHeartbeat);
    }
  }
  if(uFlowDirection!=0 && now-uFlowAnimationAt>=480) {
    uFlowAnimationAt=now;
    uBatteryFlowPhase=(uBatteryFlowPhase+1)%4;
    uRenderBatteryFlow();
  }
}

static void uBase(lv_obj_t *o) {
  lv_obj_set_pos(o,0,0);
  lv_obj_set_size(o,480,480);
  lv_obj_set_style_bg_color(o,U_BG(),0);
  lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
  lv_obj_set_style_border_width(o,0,0);
  lv_obj_set_style_pad_all(o,0,0);
  lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *uLabel(lv_obj_t *p,const char *txt,int x,int y,const lv_font_t *font,lv_color_t color) {
  lv_obj_t *l=lv_label_create(p);
  lv_label_set_text(l,txt);
  lv_obj_set_style_text_font(l,font,0);
  lv_obj_set_style_text_color(l,color,0);
  lv_obj_set_pos(l,x,y);
  return l;
}

static lv_obj_t *uCard(lv_obj_t *p,int x,int y,int w,int h,int radius=12) {
  lv_obj_t *c=lv_obj_create(p);
  lv_obj_set_pos(c,x,y); lv_obj_set_size(c,w,h);
  lv_obj_set_style_bg_color(c,U_CARD(),0);
  lv_obj_set_style_bg_opa(c,LV_OPA_COVER,0);
  lv_obj_set_style_border_color(c,U_LINE(),0);
  lv_obj_set_style_border_width(c,1,0);
  lv_obj_set_style_radius(c,radius,0);
  lv_obj_set_style_pad_all(c,0,0);
  lv_obj_clear_flag(c,LV_OBJ_FLAG_SCROLLABLE);
  return c;
}

static lv_obj_t *uMetric(lv_obj_t *p,const char *name,int x,int y,int w,int h) {
  lv_obj_t *c=uCard(p,x,y,w,h,10);
  uLabel(c,name,10,5,&lv_font_inter_14,lv_color_white());
  return c;
}

static void uPageTitle(lv_obj_t *p,const char *title,const char *badge=nullptr) {
  lv_obj_t *h=uCard(p,10,8,460,54,12);
  lv_obj_set_style_border_color(h,U_GREEN(),0);
  lv_obj_t *l=uLabel(h,title,0,8,&lv_font_inter_display_32,U_GREEN());
  lv_obj_set_width(l,460); lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);
  if(badge) {
    lv_obj_t *b=uLabel(h,badge,378,18,&lv_font_inter_14,U_GREEN());
    lv_obj_set_width(b,70); lv_obj_set_style_text_align(b,LV_TEXT_ALIGN_RIGHT,0);
  }
}

static void ui480ShowPage(uint8_t next) {
  if(uDtcPage) lv_obj_add_flag(uDtcPage,LV_OBJ_FLAG_HIDDEN);
  if(uDevPage) lv_obj_add_flag(uDevPage,LV_OBJ_FLAG_HIDDEN);
  uDtcVisible=false;
  page=next%6;
  for(int i=0;i<6;i++) {
    if(i==page) lv_obj_clear_flag(ui480Pages[i],LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(ui480Pages[i],LV_OBJ_FLAG_HIDDEN);
  }
  lastButton=millis();
  preferences.putUChar("page",page);
  uiDirty=true;
}

static void uOpenEvents(lv_event_t*) { ui480ShowPage(2); }

static void uGesture(lv_event_t *e) {
  (void)e;
  bool wizardVisible=uWizard && !lv_obj_has_flag(uWizard,LV_OBJ_FLAG_HIDDEN);
  if(wizardVisible || millis()-lastTouchNavigation<450) return;
  lv_dir_t d=lv_indev_get_gesture_dir(lv_indev_get_act());
  if(d!=LV_DIR_LEFT && d!=LV_DIR_RIGHT) return;
  lastTouchNavigation=millis();
  ui480ShowPage(d==LV_DIR_LEFT?(page+1)%6:(page+5)%6);
}

static void uDotClick(lv_event_t *e) {
  ui480ShowPage((uint8_t)(uintptr_t)lv_event_get_user_data(e));
}

static void uFooter(lv_obj_t *p,uint8_t owner) {
  for(int i=0;i<6;i++) {
    lv_obj_t *d=lv_obj_create(p);
    lv_obj_set_size(d,12,12); lv_obj_set_pos(d,183+i*23,460);
    lv_obj_set_style_radius(d,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_border_width(d,0,0); lv_obj_set_style_pad_all(d,0,0);
    lv_obj_set_style_bg_opa(d,LV_OPA_COVER,0);
    lv_obj_set_style_bg_color(d,i==owner?U_GREEN():lv_color_hex(0x40546B),0);
    lv_obj_add_flag(d,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(d,uDotClick,LV_EVENT_CLICKED,(void*)(uintptr_t)i);
  }
}

static void uBuildDashboard() {
  lv_obj_t *p=ui480Pages[0]=lv_obj_create(lv_scr_act()); uBase(p);
  uHeader=uCard(p,10,8,460,55,12);
  lv_obj_add_flag(uHeader,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(uHeader,uWizardOpen,LV_EVENT_CLICKED,nullptr);
  lv_obj_set_style_border_color(uHeader,U_GREEN(),0);
  lv_obj_t *battery=uCard(uHeader,14,13,62,29,5);
  lv_obj_set_style_bg_opa(battery,LV_OPA_TRANSP,0);
  lv_obj_set_style_border_color(battery,U_GREEN(),0);
  lv_obj_set_style_border_width(battery,2,0);
  for(int i=0;i<4;i++) {
    lv_obj_t *cell=uBatteryCells[i]=lv_obj_create(battery);
    lv_obj_set_pos(cell,5+i*12,5); lv_obj_set_size(cell,9,17);
    lv_obj_set_style_bg_color(cell,U_GREEN(),0); lv_obj_set_style_bg_opa(cell,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(cell,0,0); lv_obj_set_style_pad_all(cell,0,0); lv_obj_set_style_radius(cell,1,0);
  }
  lv_obj_t *terminal=lv_obj_create(uHeader);
  lv_obj_set_pos(terminal,76,22); lv_obj_set_size(terminal,7,11);
  lv_obj_set_style_bg_color(terminal,U_GREEN(),0); lv_obj_set_style_border_width(terminal,0,0); lv_obj_set_style_pad_all(terminal,0,0); lv_obj_set_style_radius(terminal,2,0);
  uFlowShaft=lv_line_create(uHeader);
  uFlowHead=lv_line_create(uHeader);
  for(lv_obj_t *part: {uFlowShaft,uFlowHead}) {
    lv_obj_set_style_line_width(part,5,0);
    lv_obj_set_style_line_rounded(part,true,0);
    lv_obj_clear_flag(part,LV_OBJ_FLAG_CLICKABLE);
  }
  uFlowArrowColor=U_GREEN();
  uFlowDirection=0;
  uRenderFlowArrow();
  uDirection=uLabel(uHeader,"STANDBY",122,16,&lv_font_inter_18,U_GREEN());
  lv_obj_set_width(uDirection,187);
  lv_obj_t *divider=lv_obj_create(uHeader); lv_obj_set_pos(divider,311,10); lv_obj_set_size(divider,1,35);
  lv_obj_set_style_bg_color(divider,U_LINE(),0); lv_obj_set_style_border_width(divider,0,0); lv_obj_set_style_pad_all(divider,0,0);
  uStatusTitle=uLabel(uHeader,"STATUS",323,5,&lv_font_inter_14,U_MUTED());
  uStatus=uLabel(uHeader,"WAITING",323,25,&lv_font_inter_16,U_GREEN());
  lv_obj_set_width(uStatus,125); lv_obj_set_style_text_align(uStatus,LV_TEXT_ALIGN_LEFT,0);

  uSocCard=uCard(p,10,72,268,284,12);
  uSocArc=lv_arc_create(uSocCard);
  lv_obj_set_pos(uSocArc,10,8); lv_obj_set_size(uSocArc,248,248);
  lv_arc_set_bg_angles(uSocArc,135,405); lv_arc_set_range(uSocArc,0,100); lv_arc_set_value(uSocArc,0);
  // SOC is telemetry, not a user setting. The arc must never behave like a
  // volume knob or accept drag input; its value is updated only from data.
  lv_obj_clear_flag(uSocArc,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(uSocArc,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_style(uSocArc,nullptr,LV_PART_KNOB);
  lv_obj_set_style_arc_width(uSocArc,22,LV_PART_MAIN);
  lv_obj_set_style_arc_color(uSocArc,lv_color_hex(0x253241),LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(uSocArc,true,LV_PART_MAIN);
  lv_obj_set_style_arc_width(uSocArc,22,LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(uSocArc,U_GREEN(),LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(uSocArc,true,LV_PART_INDICATOR);
  uSoc=uLabel(uSocCard,"--%",15,84,&lv_font_inter_display_48,U_GREEN());
  lv_obj_set_width(uSoc,238); lv_obj_set_style_text_align(uSoc,LV_TEXT_ALIGN_CENTER,0);
  uVolt=uLabel(uSocCard,"--.- V",15,164,&lv_font_inter_display_32,U_BLUE());
  lv_obj_set_width(uVolt,238); lv_obj_set_style_text_align(uVolt,LV_TEXT_ALIGN_CENTER,0);
  lv_obj_t *rule=lv_obj_create(uSocCard); lv_obj_set_pos(rule,54,153); lv_obj_set_size(rule,160,1);
  lv_obj_set_style_bg_color(rule,U_LINE(),0); lv_obj_set_style_border_width(rule,0,0); lv_obj_set_style_pad_all(rule,0,0);

  uPowerCard=uMetric(p,"POWER",288,72,182,135);
  uPower=uLabel(uPowerCard,"-- W",0,44,&lv_font_inter_display_48,U_BLUE());
  lv_obj_set_width(uPower,182); lv_label_set_long_mode(uPower,LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(uPower,LV_TEXT_ALIGN_CENTER,0); lv_obj_set_style_text_letter_space(uPower,-2,0);
  uCurrentCard=uMetric(p,"CURRENT",288,217,182,139);
  uCurrent=uLabel(uCurrentCard,"-- A",0,46,&lv_font_inter_display_48,U_BLUE());
  lv_obj_set_width(uCurrent,182); lv_label_set_long_mode(uCurrent,LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(uCurrent,LV_TEXT_ALIGN_CENTER,0); lv_obj_set_style_text_letter_space(uCurrent,-2,0);

  uMainBottom=uCard(p,10,365,460,74,12);
  uCellsTitle=uLabel(uMainBottom,"CELL VOLTAGE",0,3,&lv_font_inter_14,lv_color_white());
  lv_obj_set_width(uCellsTitle,150); lv_obj_set_style_text_align(uCellsTitle,LV_TEXT_ALIGN_CENTER,0);
  uCellMaxValue=uLabel(uMainBottom,"MAX  --",4,21,&lv_font_montserrat_18,U_GREEN());
  lv_obj_set_width(uCellMaxValue,142); lv_obj_set_style_text_align(uCellMaxValue,LV_TEXT_ALIGN_CENTER,0);
  uCellMinValue=uLabel(uMainBottom,"MIN   --",4,44,&lv_font_montserrat_18,U_BLUE());
  lv_obj_set_width(uCellMinValue,142); lv_obj_set_style_text_align(uCellMinValue,LV_TEXT_ALIGN_CENTER,0);
  uDeltaValue=uLabel(uMainBottom,"\xCE\x94 -- mV",150,18,&lv_font_inter_display_32,U_GREEN());
  lv_obj_set_width(uDeltaValue,160); lv_obj_set_style_text_align(uDeltaValue,LV_TEXT_ALIGN_CENTER,0);
  uTempTitle=uLabel(uMainBottom,"TEMP MAX",310,3,&lv_font_inter_14,lv_color_white());
  lv_obj_set_width(uTempTitle,150); lv_obj_set_style_text_align(uTempTitle,LV_TEXT_ALIGN_CENTER,0);
  uTempValue=uLabel(uMainBottom,"--.-\xC2\xB0",310,25,&lv_font_inter_display_32,U_BLUE());
  lv_obj_set_width(uTempValue,150); lv_obj_set_style_text_align(uTempValue,LV_TEXT_ALIGN_CENTER,0);
  for(int x: {150,310}) {
    lv_obj_t *sep=lv_obj_create(uMainBottom); lv_obj_set_pos(sep,x,12); lv_obj_set_size(sep,1,50);
    lv_obj_set_style_bg_color(sep,U_LINE(),0); lv_obj_set_style_border_width(sep,0,0); lv_obj_set_style_pad_all(sep,0,0);
  }
  uBottom=uLabel(uMainBottom,"",10,8,&lv_font_inter_16,U_RED());
  lv_obj_set_width(uBottom,440); lv_obj_set_style_text_align(uBottom,LV_TEXT_ALIGN_CENTER,0);
  lv_obj_add_flag(uBottom,LV_OBJ_FLAG_HIDDEN);
  uWarningBar=uCard(p,10,418,460,34,9);
  lv_obj_set_style_border_color(uWarningBar,U_AMBER(),0);
  lv_obj_set_style_bg_color(uWarningBar,lv_color_hex(0x241700),0);
  lv_obj_add_flag(uWarningBar,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(uWarningBar,uOpenEvents,LV_EVENT_CLICKED,nullptr);
  uWarningText=uLabel(uWarningBar,"WARNING   TAP FOR EVENT",8,7,&lv_font_inter_16,U_AMBER());
  lv_obj_set_width(uWarningText,444); lv_obj_set_style_text_align(uWarningText,LV_TEXT_ALIGN_CENTER,0);
  lv_obj_add_flag(uWarningBar,LV_OBJ_FLAG_HIDDEN);
  uFooter(p,0);

  uDualDash=lv_obj_create(p); lv_obj_set_pos(uDualDash,0,0); lv_obj_set_size(uDualDash,480,480); uBase(uDualDash);
  uPageTitle(uDualDash,"BATTERIES","MULTI");
  for(int i=0;i<3;i++){
    lv_obj_t *dc=uDualDashCard[i]=uCard(uDualDash,10,68+i*124,460,116,14);
    lv_obj_set_style_border_color(dc,i?U_BLUE():U_GREEN(),0);
    char title[20]; snprintf(title,sizeof(title),"BATTERY %d",i+1);
    uLabel(dc,title,15,9,&lv_font_montserrat_18,i?U_BLUE():U_GREEN());
    uDualDashText[i]=uLabel(dc,"WAITING FOR DATA",15,38,&lv_font_montserrat_16,lv_color_white());
    lv_obj_set_width(uDualDashText[i],430); lv_obj_set_style_text_line_space(uDualDashText[i],4,0);
  }
  uLabel(uDualDash,"TAP A BATTERY FOR DETAILS   |   SWIPE FOR CELLS",55,450,&lv_font_montserrat_12,U_MUTED());
  lv_obj_add_flag(uDualDash,LV_OBJ_FLAG_HIDDEN);
}

static void uDualPlotDraw(lv_event_t *e){
  int idx=(int)(intptr_t)lv_event_get_user_data(e);
  CellMonitorData &cs=batteryCells[idx];
  if(!cs.valid) return;
  lv_draw_ctx_t *ctx=lv_event_get_draw_ctx(e);
  lv_area_t base; lv_obj_get_coords(lv_event_get_target(e),&base);
  int lo=cs.minMv,span=max(20,(int)cs.maxMv-lo);
  lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d); d.bg_opa=LV_OPA_COVER; d.radius=1; d.border_width=0;
  for(int i=0;i<96;i++){
    if(cs.mv[i]<2000) continue;
    int ht=18+((int)cs.mv[i]-lo)*42/span; ht=constrain(ht,12,60);
    d.bg_color=i==cs.minCell?U_RED():(i==cs.maxCell?U_GREEN():lv_color_hex(0x214BFF));
    lv_area_t a={
      (lv_coord_t)(base.x1+i*4),
      (lv_coord_t)(base.y2-ht),
      (lv_coord_t)(base.x1+i*4+2),
      (lv_coord_t)base.y2
    };
    lv_draw_rect(ctx,&d,&a);
  }
}

static void uBuildCells() {
  lv_obj_t *p=ui480Pages[1]=lv_obj_create(lv_scr_act()); uBase(p); uPageTitle(p,"CELL MONITOR","96 CELLS");
  lv_obj_t *c=uMetric(p,"MIN",10,72,108,74); uCellMin=uLabel(c,"--\nCELL --",0,24,&lv_font_inter_18,U_BLUE()); lv_obj_set_width(uCellMin,108); lv_obj_set_style_text_align(uCellMin,LV_TEXT_ALIGN_CENTER,0);
  c=uMetric(p,"MAX",127,72,108,74); uCellMax=uLabel(c,"--\nCELL --",0,24,&lv_font_inter_18,U_GREEN()); lv_obj_set_width(uCellMax,108); lv_obj_set_style_text_align(uCellMax,LV_TEXT_ALIGN_CENTER,0);
  c=uMetric(p,"CELL DELTA",244,72,108,74); uCellDelta=uLabel(c,"\xCE\x94 -- mV",0,29,&lv_font_inter_18,U_GREEN()); lv_obj_set_width(uCellDelta,108); lv_obj_set_style_text_align(uCellDelta,LV_TEXT_ALIGN_CENTER,0);
  c=uMetric(p,"TEMP",361,72,109,74); uCellTemp=uLabel(c,"--\n--",0,22,&lv_font_inter_16,U_BLUE()); lv_obj_set_width(uCellTemp,109); lv_obj_set_style_text_align(uCellTemp,LV_TEXT_ALIGN_CENTER,0); lv_obj_set_style_text_line_space(uCellTemp,2,0);
  for(int i=0;i<96;i++) {
    // 96 columns span practically the full 480 px panel: 4 px bar + 1 px gap.
    uBars[i]=lv_obj_create(p); lv_obj_set_size(uBars[i],4,8); lv_obj_set_pos(uBars[i],1+i*5,420-8);
    lv_obj_set_style_bg_color(uBars[i],lv_color_hex(0x214BFF),0); lv_obj_set_style_bg_opa(uBars[i],LV_OPA_COVER,0);
    lv_obj_set_style_border_width(uBars[i],0,0); lv_obj_set_style_pad_all(uBars[i],0,0); lv_obj_set_style_radius(uBars[i],2,0);
  }
  uMinTag=uLabel(p,"",0,151,&lv_font_inter_14,U_RED());
  uMaxTag=uLabel(p,"",0,151,&lv_font_inter_14,U_GREEN());
  uFooter(p,1);

  uDualCells=lv_obj_create(p); lv_obj_set_pos(uDualCells,0,0); lv_obj_set_size(uDualCells,480,480); uBase(uDualCells);
  uPageTitle(uDualCells,"CELL MONITOR","MULTI");
  for(int i=0;i<3;i++){
    lv_obj_t *cc=uDualCellCard[i]=uCard(uDualCells,10,68+i*124,460,116,12);
    lv_obj_set_style_border_color(cc,i?U_BLUE():U_GREEN(),0);
    char title[20]; snprintf(title,sizeof(title),"BATTERY %d",i+1);
    uLabel(cc,title,12,8,&lv_font_montserrat_18,i?U_BLUE():U_GREEN());
    uDualCellText[i]=uLabel(cc,"MIN --  MAX --  CELL DELTA --  TEMP --",125,10,&lv_font_montserrat_12,lv_color_white());
    uDualCellPlot[i]=lv_obj_create(cc); lv_obj_set_pos(uDualCellPlot[i],36,38); lv_obj_set_size(uDualCellPlot[i],386,65);
    lv_obj_set_style_bg_opa(uDualCellPlot[i],LV_OPA_TRANSP,0); lv_obj_set_style_border_width(uDualCellPlot[i],0,0); lv_obj_set_style_pad_all(uDualCellPlot[i],0,0);
    lv_obj_add_event_cb(uDualCellPlot[i],uDualPlotDraw,LV_EVENT_DRAW_MAIN,(void*)(intptr_t)i);
  }
  uLabel(uDualCells,"TAP A GRAPH FOR FULL DETAIL   |   SWIPE FOR EVENTS",45,450,&lv_font_montserrat_12,U_MUTED());
  lv_obj_add_flag(uDualCells,LV_OBJ_FLAG_HIDDEN);
}

static void uBuildEvents() {
  lv_obj_t *p=ui480Pages[2]=lv_obj_create(lv_scr_act()); uBase(p); uPageTitle(p,"EVENTS");
  uEventCount=uLabel(p,"1-4 OF --   SWIPE LIST",20,70,&lv_font_montserrat_14,U_MUTED());
  uEventList=lv_obj_create(p); lv_obj_set_pos(uEventList,10,96); lv_obj_set_size(uEventList,460,345);
  lv_obj_set_style_bg_color(uEventList,U_CARD(),0); lv_obj_set_style_border_color(uEventList,U_LINE(),0); lv_obj_set_style_border_width(uEventList,1,0); lv_obj_set_style_radius(uEventList,10,0); lv_obj_set_style_pad_all(uEventList,8,0);
  lv_obj_set_flex_flow(uEventList,LV_FLEX_FLOW_COLUMN); lv_obj_set_scrollbar_mode(uEventList,LV_SCROLLBAR_MODE_AUTO);
  for(int i=0;i<10;i++) {
    uEventCards[i]=uCard(uEventList,0,0,430,68,7); lv_obj_clear_flag(uEventCards[i],LV_OBJ_FLAG_FLOATING);
    uEventRows[i]=uLabel(uEventCards[i],"",8,7,&lv_font_inter_14,lv_color_white());
    lv_obj_set_size(uEventRows[i],410,54); lv_label_set_long_mode(uEventRows[i],LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(uEventRows[i],3,0);
  }
  uEventEmpty=uLabel(p,"NO ACTIVE EVENTS",0,238,&lv_font_montserrat_20,U_GREEN());
  lv_obj_set_width(uEventEmpty,480); lv_obj_set_style_text_align(uEventEmpty,LV_TEXT_ALIGN_CENTER,0); lv_obj_add_flag(uEventEmpty,LV_OBJ_FLAG_HIDDEN);
  uEventMore=uLabel(p,"MORE",428,443,&lv_font_montserrat_12,U_GREEN());
  uFooter(p,2);
}

static void uBuildInfo() {
  lv_obj_t *p=ui480Pages[3]=lv_obj_create(lv_scr_act()); uBase(p); uPageTitle(p,"BATTERY INFO");
  lv_obj_t *hero=uCard(p,10,70,460,58,12);
  uLabel(hero,"CONTACTORS",18,19,&lv_font_montserrat_16,lv_color_white());
  uContactorState=uLabel(hero,"UNKNOWN",180,7,&lv_font_inter_display_32,U_MUTED());
  lv_obj_set_width(uContactorState,255); lv_obj_set_style_text_align(uContactorState,LV_TEXT_ALIGN_CENTER,0);

  lv_obj_t *row=uCard(p,10,134,460,44,9);
  uEmuClosing=uLabel(row,"EMULATOR ALLOWS CLOSING: --",10,11,&lv_font_montserrat_18,U_MUTED());
  lv_obj_set_width(uEmuClosing,440); lv_obj_set_style_text_align(uEmuClosing,LV_TEXT_ALIGN_CENTER,0);
  row=uCard(p,10,184,460,44,9);
  uInvClosing=uLabel(row,"INVERTER ALLOWS CLOSING: --",10,11,&lv_font_montserrat_18,U_MUTED());
  lv_obj_set_width(uInvClosing,440); lv_obj_set_style_text_align(uInvClosing,LV_TEXT_ALIGN_CENTER,0);
  row=uCard(p,10,234,460,44,9);
  uContactorWarning=uLabel(row,"CONTACTOR CONTROL STATUS: --",15,12,&lv_font_montserrat_14,U_AMBER());
  lv_obj_set_width(uContactorWarning,430); lv_obj_set_style_text_align(uContactorWarning,LV_TEXT_ALIGN_CENTER,0); lv_obj_set_style_text_line_space(uContactorWarning,0,0);

  lv_obj_t *metrics=uCard(p,10,284,460,85,10);
  uBatteryHealth=uLabel(metrics,"SOH --        REAL SOC --",10,6,&lv_font_montserrat_16,lv_color_white());
  lv_obj_set_width(uBatteryHealth,440); lv_obj_set_style_text_align(uBatteryHealth,LV_TEXT_ALIGN_CENTER,0);
  uChargeLimits=uLabel(metrics,"CHARGE LIMIT       -- kW  |  -- A",12,31,&lv_font_montserrat_18,U_GREEN());
  lv_obj_set_width(uChargeLimits,436); lv_obj_set_style_text_align(uChargeLimits,LV_TEXT_ALIGN_CENTER,0);
  uDischargeLimits=uLabel(metrics,"DISCHARGE LIMIT    -- kW  |  -- A",12,57,&lv_font_montserrat_18,U_BLUE());
  lv_obj_set_width(uDischargeLimits,436); lv_obj_set_style_text_align(uDischargeLimits,LV_TEXT_ALIGN_CENTER,0);

  uAdvancedCard=uCard(p,10,375,460,68,10);
  uHvilMetrics=uLabel(uAdvancedCard,"HVIL  --",16,9,&lv_font_montserrat_16,U_BLUE());
  lv_obj_set_width(uHvilMetrics,205); lv_obj_set_style_text_align(uHvilMetrics,LV_TEXT_ALIGN_LEFT,0);
  uBmsMetrics=uLabel(uAdvancedCard,"BMS  --",235,9,&lv_font_montserrat_16,U_BLUE());
  lv_obj_set_width(uBmsMetrics,209); lv_obj_set_style_text_align(uBmsMetrics,LV_TEXT_ALIGN_RIGHT,0);
  uIsolationMetrics=uLabel(uAdvancedCard,"ISOLATION  --",12,38,&lv_font_montserrat_18,U_BLUE());
  lv_obj_set_width(uIsolationMetrics,436); lv_obj_set_style_text_align(uIsolationMetrics,LV_TEXT_ALIGN_CENTER,0);
  if(directMode) lv_obj_add_flag(uAdvancedCard,LV_OBJ_FLAG_HIDDEN);
  uFooter(p,3);
}

static void uDevBack(lv_event_t*) {
  if(uDevPage) lv_obj_add_flag(uDevPage,LV_OBJ_FLAG_HIDDEN);
  ui480ShowPage(4);
}

static void uOpenDevelopment(const char *feature) {
  lv_label_set_text(uDevTitle,feature);
  lv_obj_clear_flag(uDevPage,LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(uDevPage);
}

static void uRotateScreen() {
  screenOrientation=(screenOrientation+1)%4;
  preferences.putUChar("screen-rot",screenOrientation);
  gfx->setRotation((1+screenOrientation)%4);
  gfx->fillScreen(BLACK);
  lv_obj_invalidate(lv_scr_act());
  char message[40];
  snprintf(message,sizeof(message),"SCREEN ROTATED: %u DEG",screenOrientation*90);
  lv_label_set_text(uActionToast,message);
  lv_obj_set_style_text_color(uActionToast,U_GREEN(),0);
  lv_obj_clear_flag(uActionToast,LV_OBJ_FLAG_HIDDEN);
  lastButton=millis();
}

static bool uWebOnlyAction(int action) {
  return action==0 || action==2 || action==4 || action==5 || action==6 ||
         action==9 || (action>=10 && action<=15);
}

static void uShowWebOnly(lv_obj_t *toast) {
  if(!toast) return;
  lv_label_set_text(toast,"WEB ONLY - SELECT WI-FI + WEB MODE");
  lv_obj_set_style_text_color(toast,U_AMBER(),0);
  lv_obj_clear_flag(toast,LV_OBJ_FLAG_HIDDEN);
}

static void uWizardOpen(lv_event_t*);

static void uAddModeBadge(lv_obj_t *button,const char *text,lv_color_t color) {
  lv_obj_t *badge=uLabel(button,text,166,2,&lv_font_inter_14,color);
  lv_obj_set_width(badge,48); lv_obj_set_style_text_align(badge,LV_TEXT_ALIGN_CENTER,0);
}

static void uMarkWebOnly(lv_obj_t *button) {
  // Make a disabled command unmistakable without relying on a tiny corner
  // badge: strike the command name and place the limitation directly below.
  lv_obj_set_style_bg_color(button,lv_color_hex(0x02060A),0);
  lv_obj_set_style_bg_opa(button,LV_OPA_COVER,0);
  lv_obj_set_style_border_color(button,lv_color_hex(0x17222D),0);
  lv_obj_set_style_border_opa(button,LV_OPA_COVER,0);
  lv_obj_t *name=lv_obj_get_child(button,0);
  if(name) {
    lv_obj_set_y(name,7);
    lv_obj_set_style_text_color(name,lv_color_hex(0x34414D),0);
  }
  lv_obj_t *strike=lv_obj_create(button);
  lv_obj_remove_style_all(strike);
  lv_obj_set_pos(strike,15,20);
  lv_obj_set_size(strike,lv_obj_get_width(button)-30,1);
  lv_obj_set_style_bg_color(strike,lv_color_hex(0x574319),0);
  lv_obj_set_style_bg_opa(strike,LV_OPA_COVER,0);
  lv_obj_clear_flag(strike,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *note=uLabel(button,"(WEB ONLY)",0,31,&lv_font_inter_14,lv_color_hex(0x806224));
  lv_obj_set_width(note,lv_obj_get_width(button));
  lv_obj_set_style_text_align(note,LV_TEXT_ALIGN_CENTER,0);
}

static void uControlClick(lv_event_t *e) {
  int action=(int)(intptr_t)lv_event_get_user_data(e);
  if(action==20) { ui480ShowPage(0); return; }
  if(action==21) { ui480ShowPage(5); return; }
  if(action==22) { uWizardOpen(e); return; }
  if(action==16) {
    uRotateScreen();
    return;
  }
  if(action==3) {
    ui480ShowPage(3);
    return;
  }
  if(action==7) {
    ui480ShowPage(1);
    return;
  }
  if(action==8) {
    ui480ShowPage(2);
    return;
  }
  if(directMode && uWebOnlyAction(action)) {
    uShowWebOnly(uActionToast);
    return;
  }
  if(action==2 || action==4 || action==5 || action==6) {
    uOpenDevelopment(action==2?"CHARGE SETTINGS":(action==4?"CAN LOGGER":(action==5?"CAN REPLAY":"LOG")));
    return;
  }
  if(action==15) {
    uDtcVisible=true;
    lv_obj_clear_flag(uDtcPage,LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(uDtcPage);
    uiDirty=true;
    return;
  }
  pendingControlAction=action;
  lv_label_set_text(uActionToast,"SENDING...");
  lv_obj_set_style_text_color(uActionToast,U_GREEN(),0);
  lv_obj_clear_flag(uActionToast,LV_OBJ_FLAG_HIDDEN);
}

static void uCriticalEvent(lv_event_t *e) {
  int action=(int)(intptr_t)lv_event_get_user_data(e);
  if(directMode && uWebOnlyAction(action)) {
    if(lv_event_get_code(e)==LV_EVENT_PRESSED) uShowWebOnly(uDtcVisible?uDtcToast:uActionToast);
    uCriticalAction=-1;
    return;
  }
  if(lv_event_get_code(e)==LV_EVENT_PRESSED){
    uCriticalPressAt=millis();
    uCriticalAction=action;
    return;
  }
  if(lv_event_get_code(e)!=LV_EVENT_RELEASED || uCriticalAction!=action) return;
  lv_obj_t *toast=uDtcVisible?uDtcToast:uActionToast;
  if(millis()-uCriticalPressAt>=2000){
    pendingControlAction=action;
    lv_label_set_text(toast,"COMMAND SENT");
    lv_obj_set_style_text_color(toast,U_GREEN(),0);
  } else {
    lv_label_set_text(toast,"HOLD FOR 2 SECONDS TO CONFIRM");
    lv_obj_set_style_text_color(toast,U_AMBER(),0);
  }
  lv_obj_clear_flag(toast,LV_OBJ_FLAG_HIDDEN);
  uCriticalAction=-1;
}

static lv_obj_t *uActionButton(lv_obj_t *p,const char *txt,int x,int y,int w,int id,lv_color_t color,bool critical=false) {
  lv_obj_t *b=uCard(p,x,y,w,54,9); lv_obj_add_flag(b,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_border_color(b,color,0); lv_obj_t *l=uLabel(b,txt,0,17,&lv_font_inter_16,color);
  lv_obj_set_width(l,w); lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);
  if(critical){
    lv_obj_add_event_cb(b,uCriticalEvent,LV_EVENT_PRESSED,(void*)(intptr_t)id);
    lv_obj_add_event_cb(b,uCriticalEvent,LV_EVENT_RELEASED,(void*)(intptr_t)id);
  } else lv_obj_add_event_cb(b,uControlClick,LV_EVENT_CLICKED,(void*)(intptr_t)id);
  return b;
}

static void uBuildControl() {
  lv_obj_t *p=ui480Pages[4]=lv_obj_create(lv_scr_act()); uBase(p); uPageTitle(p,"DISPLAY MENU");
  const char *names[]={"MAIN SCREEN","CELL MONITOR","BATTERY INFO","EVENTS","DTC / FAULTS","SYSTEM","CONNECTION SETUP","SCREEN ORIENTATION"};
  const int ids[]={20,7,3,8,15,21,22,16};
  for(int i=0;i<8;i++) {
    const lv_color_t color=ids[i]==16?U_GREEN():U_BLUE();
    uActionButton(p,names[i],10+(i%2)*230,80+(i/2)*84,220,ids[i],color);
  }
  uActionToast=uLabel(p,"",20,425,&lv_font_inter_14,U_GREEN()); lv_obj_set_width(uActionToast,440); lv_obj_set_style_text_align(uActionToast,LV_TEXT_ALIGN_CENTER,0); lv_obj_add_flag(uActionToast,LV_OBJ_FLAG_HIDDEN);
  uFooter(p,4);

  uDevPage=lv_obj_create(lv_scr_act()); uBase(uDevPage);
  lv_obj_t *back=uCard(uDevPage,10,10,54,50,9); lv_obj_add_flag(back,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *backText=uLabel(back,"<",0,10,&lv_font_inter_18,lv_color_white()); lv_obj_set_width(backText,54); lv_obj_set_style_text_align(backText,LV_TEXT_ALIGN_CENTER,0);
  lv_obj_add_event_cb(back,uDevBack,LV_EVENT_CLICKED,nullptr);
  uLabel(uDevPage,"IN DEVELOPMENT",78,17,&lv_font_inter_display_32,U_AMBER());
  lv_obj_t *panel=uCard(uDevPage,20,94,440,278,14); lv_obj_set_style_border_color(panel,U_AMBER(),0);
  uDevTitle=uLabel(panel,"FEATURE",0,38,&lv_font_inter_display_32,lv_color_white()); lv_obj_set_width(uDevTitle,440); lv_obj_set_style_text_align(uDevTitle,LV_TEXT_ALIGN_CENTER,0);
  lv_obj_t *message=uLabel(panel,"This display section is being prepared.\n\nNo command was sent to the emulator.",30,112,&lv_font_inter_16,U_MUTED());
  lv_obj_set_width(message,380); lv_obj_set_style_text_align(message,LV_TEXT_ALIGN_CENTER,0); lv_obj_set_style_text_line_space(message,8,0);
  lv_obj_t *backButton=uCard(uDevPage,80,397,320,58,10); lv_obj_add_flag(backButton,LV_OBJ_FLAG_CLICKABLE); lv_obj_set_style_border_color(backButton,U_BLUE(),0);
  lv_obj_t *backLabel=uLabel(backButton,"BACK TO WEB CONTROL",0,19,&lv_font_inter_16,U_BLUE()); lv_obj_set_width(backLabel,320); lv_obj_set_style_text_align(backLabel,LV_TEXT_ALIGN_CENTER,0);
  lv_obj_add_event_cb(backButton,uDevBack,LV_EVENT_CLICKED,nullptr);
  lv_obj_add_flag(uDevPage,LV_OBJ_FLAG_HIDDEN);
}

static void uDtcBack(lv_event_t*) {
  uDtcVisible=false;
  lv_obj_add_flag(uDtcPage,LV_OBJ_FLAG_HIDDEN);
  ui480ShowPage(4);
}

static void uDtcRefresh(lv_event_t*) {
  pendingControlAction=10;
  lv_label_set_text(uDtcToast,"REFRESH REQUESTED");
  lv_obj_set_style_text_color(uDtcToast,U_BLUE(),0);
  lv_obj_clear_flag(uDtcToast,LV_OBJ_FLAG_HIDDEN);
}

static void uBuildDtc() {
  uDtcPage=lv_obj_create(lv_scr_act()); uBase(uDtcPage);
  lv_obj_t *back=uCard(uDtcPage,8,8,48,48,8); lv_obj_add_flag(back,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *bl=uLabel(back,"<",0,7,&lv_font_montserrat_28,lv_color_white()); lv_obj_set_width(bl,48); lv_obj_set_style_text_align(bl,LV_TEXT_ALIGN_CENTER,0);
  lv_obj_add_event_cb(back,uDtcBack,LV_EVENT_CLICKED,nullptr);
  uLabel(uDtcPage,"DTC / FAULTS",68,18,&lv_font_inter_18,lv_color_white());
  uDtcSummary=uLabel(uDtcPage,"WAITING FOR /advanced",250,21,&lv_font_inter_14,U_MUTED());
  lv_obj_set_width(uDtcSummary,120); lv_obj_set_style_text_align(uDtcSummary,LV_TEXT_ALIGN_CENTER,0);
  lv_obj_t *refresh=uCard(uDtcPage,375,8,97,48,8); lv_obj_add_flag(refresh,LV_OBJ_FLAG_CLICKABLE); lv_obj_set_style_border_color(refresh,U_BLUE(),0);
  lv_obj_t *rl=uLabel(refresh,"REFRESH",0,15,&lv_font_inter_14,U_BLUE()); lv_obj_set_width(rl,97); lv_obj_set_style_text_align(rl,LV_TEXT_ALIGN_CENTER,0);
  lv_obj_add_event_cb(refresh,uDtcRefresh,LV_EVENT_CLICKED,nullptr);

  uDtcList=lv_obj_create(uDtcPage); lv_obj_set_pos(uDtcList,8,64); lv_obj_set_size(uDtcList,464,286);
  lv_obj_set_style_bg_color(uDtcList,U_BG(),0); lv_obj_set_style_bg_opa(uDtcList,LV_OPA_COVER,0); lv_obj_set_style_border_width(uDtcList,0,0);
  lv_obj_set_style_radius(uDtcList,0,0); lv_obj_set_style_pad_all(uDtcList,6,0); lv_obj_set_style_pad_row(uDtcList,6,0); lv_obj_set_flex_flow(uDtcList,LV_FLEX_FLOW_COLUMN); lv_obj_set_scrollbar_mode(uDtcList,LV_SCROLLBAR_MODE_AUTO);
  for(int i=0;i<MAX_DISPLAY_DTCS;i++){
    uDtcCards[i]=lv_obj_create(uDtcList); lv_obj_remove_style_all(uDtcCards[i]);
    lv_obj_set_size(uDtcCards[i],442,62); lv_obj_clear_flag(uDtcCards[i],LV_OBJ_FLAG_FLOATING); lv_obj_clear_flag(uDtcCards[i],LV_OBJ_FLAG_SCROLLABLE);
    uDtcRows[i]=uLabel(uDtcCards[i],"",4,2,&lv_font_inter_14,lv_color_white()); lv_obj_set_width(uDtcRows[i],432); lv_obj_set_style_text_line_space(uDtcRows[i],2,0);
  }
  uLabel(uDtcPage,"HOLD 2s TO CONFIRM",8,352,&lv_font_inter_14,U_AMBER());
  uActionButton(uDtcPage,"CLEAR ISOLATION",8,370,151,11,U_AMBER(),true);
  uActionButton(uDtcPage,"BMS RESET",165,370,148,12,U_RED(),true);
  uActionButton(uDtcPage,"SOC RESET",319,370,153,13,U_RED(),true);
  uDtcToast=uLabel(uDtcPage,"",10,431,&lv_font_inter_14,U_GREEN()); lv_obj_set_width(uDtcToast,460); lv_obj_set_style_text_align(uDtcToast,LV_TEXT_ALIGN_CENTER,0); lv_obj_add_flag(uDtcToast,LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(uDtcPage,LV_OBJ_FLAG_HIDDEN);
}

static void uWizardOpen(lv_event_t*) {
  configPortalStopRequested=false;
  configPortalRequested=true;
  uWizardDirectMode=directMode;
  uWizardSaveError="";
  uWizardStep=0;
  uWizardRender();
}

static void uBuildSystem() {
  lv_obj_t *p=ui480Pages[5]=lv_obj_create(lv_scr_act()); uBase(p); uPageTitle(p,"SYSTEM");
  lv_obj_t *c=uCard(p,10,75,460,292,12);
  uSystemInfo=uLabel(c,"",18,18,&lv_font_montserrat_16,lv_color_white()); lv_obj_set_style_text_line_space(uSystemInfo,9,0); lv_obj_set_width(uSystemInfo,425);
  lv_obj_t *settings=uCard(p,10,380,460,62,10); lv_obj_add_flag(settings,LV_OBJ_FLAG_CLICKABLE); lv_obj_set_style_border_color(settings,U_BLUE(),0);
  lv_obj_t *sl=uLabel(settings,"CONNECTION SETUP",0,0,&lv_font_montserrat_16,U_BLUE());
  lv_obj_set_size(sl,460,62); lv_obj_set_style_text_align(sl,LV_TEXT_ALIGN_CENTER,0); lv_obj_set_style_pad_top(sl,20,0);
  // The label covers most of the card. Make it an explicit hit target instead
  // of relying on LVGL to pass the click through to its parent card.
  lv_obj_add_flag(sl,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(settings,uWizardOpen,LV_EVENT_CLICKED,nullptr);
  lv_obj_add_event_cb(sl,uWizardOpen,LV_EVENT_CLICKED,nullptr);
  uFooter(p,5);
}

static lv_obj_t *uWizardButton(const char *txt,int x,int y,int w,lv_event_cb_t cb,void *data=nullptr,lv_color_t color=lv_color_hex(0x16C9F2)) {
  lv_obj_t *b=uCard(uWizard,x,y,w,58,11); lv_obj_add_flag(b,LV_OBJ_FLAG_CLICKABLE); lv_obj_set_style_border_color(b,color,0);
  lv_obj_t *l=uLabel(b,txt,0,18,&lv_font_montserrat_16,color); lv_obj_set_width(l,w); lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);
  lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,data); return b;
}

static const char *uKbLower[]={
  "1","2","3","4","5","6","7","8","9","0","DEL","\n",
  "q","w","e","r","t","y","u","i","o","p","\n",
  "a","s","d","f","g","h","j","k","l","@","\n",
  "SHIFT","z","x","c","v","b","n","m",".","-","\n",
  "SYM","_","SPACE","/","DONE",""
};
static const char *uKbUpper[]={
  "1","2","3","4","5","6","7","8","9","0","DEL","\n",
  "Q","W","E","R","T","Y","U","I","O","P","\n",
  "A","S","D","F","G","H","J","K","L","@","\n",
  "abc","Z","X","C","V","B","N","M",".","-","\n",
  "SYM","_","SPACE","/","DONE",""
};
static const char *uKbSymbols[]={
  "1","2","3","4","5","6","7","8","9","0","DEL","\n",
  "!","@","#","$","%","^","&","*","(",")","\n",
  "+","=","[","]","{","}","<",">","?","~","\n",
  "abc","_","-",".",",",":",";","'","\"","\n",
  "SPACE","/","\\","DONE",""
};
static const char *uKbNumber[]={
  "1","2","3","\n","4","5","6","\n","7","8","9","\n","DEL","0",".","DONE",""
};

static void uKeyboardEvent(lv_event_t *e) {
  if(lv_event_get_code(e)!=LV_EVENT_VALUE_CHANGED || !uKeyboardTarget) return;
  lv_obj_t *kb=lv_event_get_target(e);
  uint16_t id=lv_btnmatrix_get_selected_btn(kb);
  const char *txt=lv_btnmatrix_get_btn_text(kb,id);
  if(!txt) return;
  if(!strcmp(txt,"SHIFT")){ uKeyboardUpper=true; uKeyboardSymbols=false; lv_btnmatrix_set_map(kb,uKbUpper); }
  else if(!strcmp(txt,"abc")){ uKeyboardUpper=false; uKeyboardSymbols=false; lv_btnmatrix_set_map(kb,uKbLower); }
  else if(!strcmp(txt,"SYM")){ uKeyboardSymbols=true; lv_btnmatrix_set_map(kb,uKbSymbols); }
  else if(!strcmp(txt,"DEL")) lv_textarea_del_char(uKeyboardTarget);
  else if(!strcmp(txt,"SPACE")) lv_textarea_add_char(uKeyboardTarget,' ');
  else if(!strcmp(txt,"DONE")) lv_obj_add_flag(kb,LV_OBJ_FLAG_HIDDEN);
  else lv_textarea_add_text(uKeyboardTarget,txt);
}

static void uKeyboardFocus(lv_event_t *e) {
  uKeyboardTarget=lv_event_get_target(e);
  if(uKeyboard) lv_obj_clear_flag(uKeyboard,LV_OBJ_FLAG_HIDDEN);
}
static void uStyleKeyboard(lv_obj_t *kb) {
  lv_obj_set_style_bg_color(kb,U_BG(),LV_PART_MAIN);
  lv_obj_set_style_bg_opa(kb,LV_OPA_COVER,LV_PART_MAIN);
  lv_obj_set_style_border_width(kb,0,LV_PART_MAIN);
  lv_obj_set_style_pad_all(kb,3,LV_PART_MAIN);
  lv_obj_set_style_bg_color(kb,lv_color_hex(0x12304A),LV_PART_ITEMS);
  lv_obj_set_style_bg_opa(kb,LV_OPA_COVER,LV_PART_ITEMS);
  lv_obj_set_style_text_color(kb,lv_color_white(),LV_PART_ITEMS);
  lv_obj_set_style_border_color(kb,U_BLUE(),LV_PART_ITEMS);
  lv_obj_set_style_border_width(kb,1,LV_PART_ITEMS);
  lv_obj_set_style_radius(kb,6,LV_PART_ITEMS);
  lv_obj_set_style_text_font(kb,&lv_font_montserrat_14,LV_PART_ITEMS);
  lv_obj_set_style_pad_all(kb,2,LV_PART_ITEMS);
}
static lv_obj_t *uCreateKeyboard(int y,int h,lv_obj_t *target,bool numeric=false) {
  uKeyboardTarget=target; uKeyboardUpper=false; uKeyboardSymbols=false;
  lv_obj_t *kb=lv_btnmatrix_create(uWizard);
  lv_obj_set_pos(kb,0,y); lv_obj_set_size(kb,480,h);
  lv_btnmatrix_set_map(kb,numeric?uKbNumber:uKbLower);
  uStyleKeyboard(kb);
  lv_obj_add_event_cb(kb,uKeyboardEvent,LV_EVENT_VALUE_CHANGED,nullptr);
  lv_obj_move_foreground(kb);
  lv_obj_invalidate(kb);
  return kb;
}
static void uStyleTextArea(lv_obj_t *field) {
  lv_obj_set_style_bg_color(field,U_CARD(),LV_PART_MAIN);
  lv_obj_set_style_bg_opa(field,LV_OPA_COVER,LV_PART_MAIN);
  lv_obj_set_style_border_color(field,U_BLUE(),LV_PART_MAIN);
  lv_obj_set_style_border_width(field,2,LV_PART_MAIN);
  lv_obj_set_style_radius(field,10,LV_PART_MAIN);
  lv_obj_set_style_text_color(field,lv_color_white(),LV_PART_MAIN);
  lv_obj_set_style_text_font(field,&lv_font_montserrat_18,LV_PART_MAIN);
  lv_obj_set_style_pad_left(field,14,LV_PART_MAIN);
  lv_obj_set_style_pad_right(field,14,LV_PART_MAIN);
  lv_obj_set_style_outline_width(field,0,LV_STATE_FOCUSED);
  lv_obj_set_style_border_color(field,U_GREEN(),LV_STATE_FOCUSED);
  lv_obj_set_style_bg_color(field,U_GREEN(),LV_PART_CURSOR);
}
static void uWizardBack(lv_event_t*) {
  if(uWizardStep) uWizardStep--;
  else {
    lv_obj_add_flag(uWizard,LV_OBJ_FLAG_HIDDEN);
    configPortalStopRequested=true;
    return;
  }
  uWizardRender();
}
static void uWizardMode(lv_event_t *e) { uWizardDirectMode=(bool)(intptr_t)lv_event_get_user_data(e); uWizardSaveError=""; uWizardStep=uWizardDirectMode?3:1; uWizardRender(); }
static void uWizardNext(lv_event_t*) {
  if(uWizardStep==1){ wifiSsid=lv_textarea_get_text(uSsid); wifiPassword=lv_textarea_get_text(uPassword); if(!wifiSsid.length()) return; uWizardStep=2; }
  else if(uWizardStep==2){ emulatorHost=lv_textarea_get_text(uHost); if(!emulatorHost.length()) return; uWizardStep=3; }
  uWizardRender();
}
static void uWizardSave(lv_event_t*) {
  wifiSsid.trim(); emulatorHost.trim();
  if(!uWizardDirectMode && (!wifiSsid.length() || !emulatorHost.length())) {
    uWizardSaveError="WI-FI NAME AND EMULATOR IP ARE REQUIRED";
    uWizardRender();
    return;
  }
  // Stage the selected mode in NVS. The active radio mode is deliberately not
  // changed here: boot applies the verified setting after a clean restart.
  bool saved=saveConnectionMode(uWizardDirectMode);
  if(saved) {
    preferences.putBool("ui13done",true);
    preferences.putUChar("bright",255);
  }
  if(!saved) {
    uWizardSaveError="SAVE FAILED - PLEASE TRY AGAIN";
    uWizardRender();
    return;
  }
  brightness=255; onboardingNeeded=false; configPortalExitAt=millis()+200; settingsRestartAt=millis()+1000;
  lv_obj_add_flag(uWizard,LV_OBJ_FLAG_HIDDEN);
}

static void uWizardRender() {
  if(!uWizard) { uWizard=lv_obj_create(lv_scr_act()); lv_obj_set_size(uWizard,480,480); lv_obj_set_pos(uWizard,0,0); }
  uSsid=nullptr; uPassword=nullptr; uHost=nullptr; uKeyboard=nullptr; uKeyboardTarget=nullptr;
  lv_obj_clean(uWizard); uBase(uWizard); lv_obj_clear_flag(uWizard,LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(uWizard);
  lv_obj_t *h=uCard(uWizard,10,8,460,55,12); lv_obj_set_style_border_color(h,U_GREEN(),0);
  lv_obj_t *title=uLabel(h,uWizardStep==0?"SELECT CONNECTION":uWizardStep==1?"HOME WI-FI":uWizardStep==2?"EMULATOR ADDRESS":"READY TO START",0,14,&lv_font_montserrat_22,U_GREEN()); lv_obj_set_width(title,460); lv_obj_set_style_text_align(title,LV_TEXT_ALIGN_CENTER,0);
  if(uWizardStep==0) {
    uLabel(uWizard,"Choose how the display receives battery data.",30,92,&lv_font_montserrat_16,U_MUTED());
    uWizardButton("ESP-NOW DIRECT",25,145,430,uWizardMode,(void*)1,U_GREEN());
    uLabel(uWizard,"Direct radio link. No router is required.",55,210,&lv_font_montserrat_14,U_MUTED());
    uWizardButton("WI-FI + WEB CONTROL",25,270,430,uWizardMode,(void*)0,U_BLUE());
    uLabel(uWizard,"Data and all emulator web controls over your network.",35,335,&lv_font_montserrat_14,U_MUTED());
    uLabel(uWizard,"PHONE: BatteryEmulator-CYD  |  192.168.4.1",66,380,&lv_font_montserrat_12,U_AMBER());
    uWizardButton("EXIT",150,405,180,uWizardBack,nullptr,U_MUTED());
  } else if(uWizardStep==1) {
    uLabel(uWizard,"NETWORK NAME",112,76,&lv_font_montserrat_14,U_MUTED());
    uSsid=lv_textarea_create(uWizard); lv_obj_set_pos(uSsid,112,98); lv_obj_set_size(uSsid,348,52); lv_textarea_set_one_line(uSsid,true); uStyleTextArea(uSsid); lv_textarea_set_placeholder_text(uSsid,"ENTER NETWORK NAME"); lv_textarea_set_text(uSsid,wifiSsid.c_str());
    uLabel(uWizard,"PASSWORD",20,156,&lv_font_montserrat_14,U_MUTED());
    uPassword=lv_textarea_create(uWizard); lv_obj_set_pos(uPassword,20,178); lv_obj_set_size(uPassword,330,52); lv_textarea_set_one_line(uPassword,true); uStyleTextArea(uPassword); lv_textarea_set_placeholder_text(uPassword,"ENTER PASSWORD"); lv_textarea_set_password_mode(uPassword,true); lv_textarea_set_text(uPassword,wifiPassword.c_str());
    uWizardButton("NEXT",360,174,100,uWizardNext,nullptr,U_GREEN());
    uKeyboard=uCreateKeyboard(240,240,uSsid,false);
    lv_obj_add_event_cb(uSsid,uKeyboardFocus,LV_EVENT_CLICKED,nullptr); lv_obj_add_event_cb(uPassword,uKeyboardFocus,LV_EVENT_CLICKED,nullptr);
    uWizardButton("BACK",10,72,92,uWizardBack,nullptr,U_MUTED());
  } else if(uWizardStep==2) {
    uLabel(uWizard,"Battery Emulator IP address",112,82,&lv_font_montserrat_16,U_MUTED());
    uHost=lv_textarea_create(uWizard); lv_obj_set_pos(uHost,112,125); lv_obj_set_size(uHost,230,58); lv_textarea_set_one_line(uHost,true); uStyleTextArea(uHost); lv_textarea_set_placeholder_text(uHost,"ENTER IP ADDRESS"); lv_textarea_set_text(uHost,emulatorHost.c_str());
    uWizardButton("NEXT",355,125,105,uWizardNext,nullptr,U_GREEN());
    uKeyboard=uCreateKeyboard(210,270,uHost,true);
    lv_obj_add_event_cb(uHost,uKeyboardFocus,LV_EVENT_CLICKED,nullptr);
    uWizardButton("BACK",10,72,92,uWizardBack,nullptr,U_MUTED());
  } else {
    String s=String("MODE: ")+(uWizardDirectMode?"ESP-NOW DIRECT":"WI-FI + WEB CONTROL");
    if(!uWizardDirectMode) s+="\nWI-FI: "+wifiSsid+"\nEMULATOR: "+emulatorHost;
    else s+="\nCHANNEL: "+String(espNowChannel)+"\nRECEIVER: "+WiFi.macAddress();
    lv_obj_t *sum=uLabel(uWizard,s.c_str(),30,100,&lv_font_montserrat_20,lv_color_white()); lv_obj_set_style_text_line_space(sum,14,0);
    if(uWizardSaveError.length()) {
      lv_obj_t *error=uLabel(uWizard,uWizardSaveError.c_str(),20,260,&lv_font_montserrat_14,U_RED());
      lv_obj_set_width(error,440); lv_obj_set_style_text_align(error,LV_TEXT_ALIGN_CENTER,0);
    }
    uWizardButton("SAVE AND START",25,310,430,uWizardSave,nullptr,U_GREEN());
    uWizardButton("BACK",25,385,200,uWizardBack,nullptr,U_MUTED());
  }
}

static void ui480Build() {
  uBuildDashboard(); uBuildCells(); uBuildEvents(); uBuildInfo(); uBuildControl(); uBuildSystem(); uBuildDtc();
  // Child objects keep LVGL's default GESTURE_BUBBLE flag. The active screen
  // receives the gesture selected through LVGL's normal hit-test chain.
  lv_obj_add_event_cb(lv_scr_act(),uGesture,LV_EVENT_GESTURE,nullptr);
  for(int i=1;i<6;i++) lv_obj_add_flag(ui480Pages[i],LV_OBJ_FLAG_HIDDEN);
  ui480ShowPage(page<6?page:0);
  if(onboardingNeeded) uWizardRender();
  uHeartbeat=lv_obj_create(lv_scr_act());
  lv_obj_remove_style_all(uHeartbeat);
  lv_obj_set_pos(uHeartbeat,464,464); lv_obj_set_size(uHeartbeat,8,8);
  lv_obj_set_style_radius(uHeartbeat,LV_RADIUS_CIRCLE,0);
  lv_obj_set_style_bg_color(uHeartbeat,U_GREEN(),0); lv_obj_set_style_bg_opa(uHeartbeat,LV_OPA_COVER,0);
  lv_obj_set_style_shadow_color(uHeartbeat,U_GREEN(),0); lv_obj_set_style_shadow_width(uHeartbeat,7,0); lv_obj_set_style_shadow_opa(uHeartbeat,LV_OPA_60,0);
  lv_obj_clear_flag(uHeartbeat,LV_OBJ_FLAG_CLICKABLE); lv_obj_clear_flag(uHeartbeat,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_move_foreground(uHeartbeat);
}

static const char *uHealthText(Health h) {
  static char scan[40];
  if(directMode && !espNowChannelLocked){
    snprintf(scan,sizeof(scan),"SCANNING ESP-NOW  CH %u",espNowChannel);
    return scan;
  }
  if(h==H_OFFLINE) return "OFFLINE";
  if(h==H_WARN) return eventSeverityRank()>=2?"FAULT":"WARNING";
  static String status;
  status=t.powerStatus.length()?t.powerStatus:"RUNNING";
  status.toUpperCase();
  int colon=status.lastIndexOf(':');
  if(colon>=0) status=status.substring(colon+1);
  status.trim();
  return status.length()?status.c_str():"RUNNING";
}

static void uShowMainMetrics(bool show) {
  lv_obj_t *metrics[]={uCellsTitle,uCellMaxValue,uCellMinValue,uDeltaValue,uTempTitle,uTempValue};
  for(lv_obj_t *metric: metrics) {
    if(show) lv_obj_clear_flag(metric,LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(metric,LV_OBJ_FLAG_HIDDEN);
  }
  if(show) lv_obj_add_flag(uBottom,LV_OBJ_FLAG_HIDDEN);
  else lv_obj_clear_flag(uBottom,LV_OBJ_FLAG_HIDDEN);
}

static void ui480Update() {
  Health h=health(); char b[512];
  uHeartbeatLevel=h==H_OFFLINE?2:(h==H_WARN?1:0);
  int severity=eventsFresh?eventSeverityRank():0;
  int warningEventIndex=eventsFresh?firstWarningEventIndex():-1;
  lv_color_t state=h==H_OFFLINE?U_RED():(h==H_WARN?(severity>=2?U_RED():U_AMBER()):U_GREEN());
  lv_obj_set_style_border_color(uHeader,state,0);
  const char *healthText=uHealthText(h);
  lv_label_set_text(uStatus,healthText); lv_obj_set_style_text_color(uStatus,state,0);
  lv_obj_set_pos(uStatusTitle,323,5);
  lv_obj_set_pos(uStatus,323,25);
  lv_obj_set_width(uStatus,125);
  lv_obj_set_style_text_font(uStatus,&lv_font_inter_16,0);
  const bool offline=h==H_OFFLINE;
  const char *flow=offline?"NO LIVE DATA":(t.current>0.05f?"CHARGING":(t.current<-0.05f?"DISCHARGING":"STANDBY"));
  lv_color_t headerFlowColor=offline?U_RED():(t.current>0.05f?U_GREEN():(t.current<-0.05f?U_BLUE():state));
  lv_label_set_text(uDirection,flow); lv_obj_set_style_text_color(uDirection,headerFlowColor,0);
  // Positive current is charging, so the arrow must point left into the pack.
  // Negative current is discharge, so it points right away from the pack.
  uSetFlowArrow(offline?0:(t.current>0.05f?-1:(t.current<-0.05f?1:0)),headerFlowColor);
  int soc=offline||isnan(t.soc)?0:constrain((int)lroundf(t.soc),0,100); lv_arc_set_value(uSocArc,soc);
  lv_color_t socArcColor=offline?U_RED():(t.current<-0.05f?U_BLUE():U_GREEN());
  lv_obj_set_style_arc_color(uSocArc,socArcColor,LV_PART_INDICATOR);
  snprintf(b,sizeof(b),offline||isnan(t.soc)?"--%%":"%.1f%%",t.soc); lv_label_set_text(uSoc,b); lv_obj_set_style_text_color(uSoc,socArcColor,0);
  snprintf(b,sizeof(b),offline||isnan(t.voltage)?"--.- V":"%.1f V",t.voltage); lv_label_set_text(uVolt,b); lv_obj_set_style_text_color(uVolt,offline?U_RED():U_BLUE(),0);
  String pText=offline?String("-- W"):powerText();
  snprintf(b,sizeof(b),offline||isnan(t.current)?"-- A":"%+.1f A",t.current);
  String cText=b;
  // POWER and CURRENT always use the same complete font and size.  If either
  // value becomes too long for its card, both rows step down together.
  bool compactMetrics=uTextWidth(pText,&lv_font_inter_display_48)>174 ||
                      uTextWidth(cText,&lv_font_inter_display_48)>174;
  const lv_font_t *metricFont=compactMetrics?&lv_font_inter_display_32:&lv_font_inter_display_48;
  int metricY=compactMetrics?54:44;
  lv_obj_set_style_text_font(uPower,metricFont,0); lv_obj_set_y(uPower,metricY);
  lv_obj_set_style_text_font(uCurrent,metricFont,0); lv_obj_set_y(uCurrent,metricY+2);
  lv_label_set_text(uPower,pText.c_str()); lv_label_set_text(uCurrent,cText.c_str());
  lv_color_t flowColor=offline?U_RED():(isnan(t.current)?U_MUTED():(t.current>0.05f?U_GREEN():(t.current<-0.05f?U_BLUE():U_MUTED()))); lv_obj_set_style_text_color(uPower,flowColor,0); lv_obj_set_style_text_color(uCurrent,flowColor,0);

  if(offline){
    uShowMainMetrics(false);
    String lines="ACTIVE EVENTS";
    if(evCount<=0) lines+="\nCRITICAL  Battery data timeout";
    for(int i=0;i<min(evCount,2);i++) lines+="\n"+ev[i].severity+"  "+shortEventText(ev[i]);
    lv_label_set_text(uBottom,lines.c_str());
    lv_obj_set_style_text_color(uBottom,U_RED(),0);
    lv_obj_set_style_border_color(uMainBottom,U_RED(),0);
    lv_obj_set_y(uMainBottom,365); lv_obj_set_height(uMainBottom,84); lv_obj_set_y(uBottom,8);
    lv_obj_add_flag(uWarningBar,LV_OBJ_FLAG_HIDDEN);
  } else {
    uShowMainMetrics(true);
    snprintf(b,sizeof(b),"MAX %.3f V",t.cMax/1000.0f); lv_label_set_text(uCellMaxValue,b);
    snprintf(b,sizeof(b),"MIN  %.3f V",t.cMin/1000.0f); lv_label_set_text(uCellMinValue,b);
    snprintf(b,sizeof(b),"\xCE\x94 %u mV",t.delta); lv_label_set_text(uDeltaValue,b);
    snprintf(b,sizeof(b),"%.1f\xC2\xB0",t.tMax); lv_label_set_text(uTempValue,b);
    lv_color_t deltaColor=t.delta<=20?U_GREEN():(t.delta<=50?U_AMBER():U_RED());
    lv_obj_set_style_text_color(uDeltaValue,deltaColor,0);
    const bool minFault=t.cMin>0 && t.cMin<CELL_LOW_WARNING_MV;
    const bool maxFault=t.cMax>CELL_HIGH_WARNING_MV;
    lv_obj_set_style_text_color(uCellMinValue,minFault?U_RED():U_BLUE(),0);
    lv_obj_set_style_text_color(uCellMaxValue,maxFault?U_RED():U_GREEN(),0);
    lv_obj_set_style_border_color(uMainBottom,U_LINE(),0);
    if(warningEventIndex>=0){
      lv_obj_set_y(uCellsTitle,0);
      lv_obj_set_y(uCellMaxValue,16); lv_obj_set_y(uCellMinValue,35);
      lv_obj_set_style_text_font(uCellMaxValue,&lv_font_montserrat_16,0); lv_obj_set_style_text_font(uCellMinValue,&lv_font_montserrat_16,0);
      lv_obj_set_y(uDeltaValue,11); lv_obj_set_y(uTempTitle,0); lv_obj_set_y(uTempValue,17);
      lv_obj_set_y(uMainBottom,356); lv_obj_set_height(uMainBottom,58); lv_obj_set_y(uBottom,18);
      String warn=shortEventText(ev[warningEventIndex]);
      lv_label_set_text(uWarningText,(warn+"   TAP FOR EVENT").c_str());
      lv_obj_set_style_text_color(uWarningText,U_AMBER(),0); lv_obj_set_style_border_color(uWarningBar,U_AMBER(),0);
      lv_obj_clear_flag(uWarningBar,LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_set_y(uCellsTitle,3);
      lv_obj_set_y(uCellMaxValue,21); lv_obj_set_y(uCellMinValue,44);
      lv_obj_set_style_text_font(uCellMaxValue,&lv_font_montserrat_18,0); lv_obj_set_style_text_font(uCellMinValue,&lv_font_montserrat_18,0);
      lv_obj_set_y(uDeltaValue,18); lv_obj_set_y(uTempTitle,3); lv_obj_set_y(uTempValue,25);
      lv_obj_set_y(uMainBottom,365); lv_obj_set_height(uMainBottom,74); lv_obj_set_y(uBottom,23);
      lv_obj_add_flag(uWarningBar,LV_OBJ_FLAG_HIDDEN);
    }
  }

  if(cells.valid) {
    snprintf(b,sizeof(b),"%u mV\nCELL %u",cells.minMv,cells.minCell+1); lv_label_set_text(uCellMin,b);
    snprintf(b,sizeof(b),"%u mV\nCELL %u",cells.maxMv,cells.maxCell+1); lv_label_set_text(uCellMax,b);
    snprintf(b,sizeof(b),"\xCE\x94 %u mV",cells.deltaMv); lv_label_set_text(uCellDelta,b);
    snprintf(b,sizeof(b),"%.1f C\n%.1f C",t.tMin,t.tMax); lv_label_set_text(uCellTemp,b);
    const bool minFault=cells.minMv>0 && cells.minMv<CELL_LOW_WARNING_MV;
    const bool maxFault=cells.maxMv>CELL_HIGH_WARNING_MV;
    lv_obj_set_style_text_color(uCellMin,minFault?U_RED():U_BLUE(),0);
    lv_obj_set_style_text_color(uCellMax,maxFault?U_RED():U_GREEN(),0);
    lv_obj_set_style_text_color(uMinTag,minFault?U_RED():U_BLUE(),0);
    lv_obj_set_style_text_color(uMaxTag,maxFault?U_RED():U_GREEN(),0);
    int lo=cells.minMv, span=max(20,(int)cells.maxMv-lo);
    for(int i=0;i<96;i++) {
      if(cells.mv[i]<2000) { lv_obj_add_flag(uBars[i],LV_OBJ_FLAG_HIDDEN); continue; }
      lv_obj_clear_flag(uBars[i],LV_OBJ_FLAG_HIDDEN);
      int ht=75+((int)cells.mv[i]-lo)*100/span; ht=constrain(ht,65,175);
      lv_obj_set_y(uBars[i],420-ht); lv_obj_set_height(uBars[i],ht);
      lv_obj_set_style_bg_color(uBars[i],i==cells.minCell?(minFault?U_RED():U_BLUE()):(i==cells.maxCell?(maxFault?U_RED():U_GREEN()):lv_color_hex(0x214BFF)),0);
    }
    snprintf(b,sizeof(b),"%u MIN",cells.minCell+1); lv_label_set_text(uMinTag,b);
    snprintf(b,sizeof(b),"%u MAX",cells.maxCell+1); lv_label_set_text(uMaxTag,b);
    int minTagX=constrain((int)cells.minCell*5-12,2,414);
    int maxTagX=constrain((int)cells.maxCell*5-12,2,414);
    // Labels over nearby extrema must remain side-by-side instead of being
    // painted on top of one another.
    if(abs(minTagX-maxTagX)<68){
      int pairLeft=constrain((minTagX+maxTagX)/2-58,2,356);
      minTagX=pairLeft;
      maxTagX=pairLeft+64;
    }
    lv_obj_set_x(uMinTag,minTagX);
    lv_obj_set_x(uMaxTag,maxTagX);
  }
    if(evCount<=0) {
      lv_label_set_text(uEventCount,eventsFresh?"NO EVENTS":"LOADING EVENTS...");
      lv_obj_set_scrollbar_mode(uEventList,LV_SCROLLBAR_MODE_OFF);
      lv_obj_clear_flag(uEventEmpty,LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(uEventMore,LV_OBJ_FLAG_HIDDEN);
      for(int i=0;i<10;i++) { lv_label_set_text(uEventRows[i],""); lv_obj_add_flag(uEventCards[i],LV_OBJ_FLAG_HIDDEN); }
    } else {
      snprintf(b,sizeof(b),"1-%d OF %d   SWIPE LIST",min(4,evCount),evCount); lv_label_set_text(uEventCount,b);
      lv_obj_add_flag(uEventEmpty,LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_scrollbar_mode(uEventList,evCount>4?LV_SCROLLBAR_MODE_AUTO:LV_SCROLLBAR_MODE_OFF);
      if(evCount>4) lv_obj_clear_flag(uEventMore,LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(uEventMore,LV_OBJ_FLAG_HIDDEN);
      for(int i=0;i<10;i++) {
        if(i<evCount) {
          lv_obj_clear_flag(uEventCards[i],LV_OBJ_FLAG_HIDDEN);
          String s=String(i+1)+"  "+ev[i].severity+"  "+fullEventText(ev[i]); lv_label_set_text(uEventRows[i],s.c_str());
          String sev=ev[i].severity; sev.toUpperCase(); lv_obj_set_style_text_color(uEventRows[i],sev.indexOf("ERROR")>=0?U_RED():(sev.indexOf("WARN")>=0?U_AMBER():U_GREEN()),0);
        } else { lv_label_set_text(uEventRows[i],""); lv_obj_add_flag(uEventCards[i],LV_OBJ_FLAG_HIDDEN); }
      }
    }
  String contactor=t.hvpContactorState!="--"?t.hvpContactorState:(t.emuCont&&t.invCont?"CLOSED":"UNKNOWN");
  String contactorUpper=contactor; contactorUpper.toUpperCase();
  lv_label_set_text(uContactorState,contactor.c_str());
  lv_obj_set_style_text_color(uContactorState,contactorUpper.indexOf("CLOSED")>=0?U_GREEN():(contactorUpper.indexOf("OPEN")>=0?U_RED():U_AMBER()),0);
  lv_label_set_text(uEmuClosing,t.emuCont?"EMULATOR ALLOWS CLOSING: YES":"EMULATOR ALLOWS CLOSING: NO");
  lv_obj_set_style_text_color(uEmuClosing,t.emuCont?U_GREEN():U_RED(),0);
  lv_label_set_text(uInvClosing,t.invCont?"INVERTER ALLOWS CLOSING: YES":"INVERTER ALLOWS CLOSING: NO");
  lv_obj_set_style_text_color(uInvClosing,t.invCont?U_GREEN():U_RED(),0);
  lv_label_set_text(uContactorWarning,t.contactorWarning?"WARNING: CONTACTORS NOT FULLY CONTROLLED\nBY EMULATOR":"CONTACTOR CONTROL STATUS: OK");
  lv_obj_set_y(uContactorWarning,t.contactorWarning?4:12);
  lv_obj_set_style_text_color(uContactorWarning,t.contactorWarning?U_SOFT_AMBER():U_GREEN(),0);
  snprintf(b,sizeof(b),"SOH %.0f%%        REAL SOC %.1f%%",t.soh,t.realSoc); lv_label_set_text(uBatteryHealth,b);
  snprintf(b,sizeof(b),"CHARGE LIMIT       %.1f kW  |  %.1f A",t.maxChgKw,t.maxChgA); lv_label_set_text(uChargeLimits,b);
  snprintf(b,sizeof(b),"DISCHARGE LIMIT    %.1f kW  |  %.1f A",t.maxDisKw,t.maxDisA); lv_label_set_text(uDischargeLimits,b);
  if(directMode) {
    lv_obj_add_flag(uAdvancedCard,LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(uAdvancedCard,LV_OBJ_FLAG_HIDDEN);
    snprintf(b,sizeof(b),"HVIL  %s",t.hvilStatus.c_str()); lv_label_set_text(uHvilMetrics,b);
    lv_obj_set_style_text_color(uHvilMetrics,t.hvilStatus.indexOf("OK")>=0?U_GREEN():U_AMBER(),0);
    snprintf(b,sizeof(b),"BMS  %s",t.bmsState.c_str()); lv_label_set_text(uBmsMetrics,b);
    if(isnan(t.isolationKOhm)) snprintf(b,sizeof(b),"ISOLATION  --");
    else snprintf(b,sizeof(b),"ISOLATION  %.0f kOhm",t.isolationKOhm);
    lv_label_set_text(uIsolationMetrics,b);
  }

  snprintf(b,sizeof(b),dtcFresh?"%u / %u MATCHED":"WAITING",dtcCount,dtcCount); lv_label_set_text(uDtcSummary,b);
  for(int i=0;i<MAX_DISPLAY_DTCS;i++){
    if(i<dtcCount){
      lv_obj_clear_flag(uDtcCards[i],LV_OBJ_FLAG_HIDDEN);
      String row=String(i+1)+"  "+dtcItems[i].ecu+" "+dtcItems[i].frame+"   "+dtcItems[i].status+"\n"+dtcItems[i].title+"\n"+dtcItems[i].shortName;
      lv_label_set_text(uDtcRows[i],row.c_str());
      lv_obj_set_style_text_color(uDtcRows[i],dtcItems[i].status=="ACTIVE"?U_RED():(dtcItems[i].status=="CONFIRMED"?U_AMBER():U_BLUE()),0);
    } else lv_obj_add_flag(uDtcCards[i],LV_OBJ_FLAG_HIDDEN);
  }
  String ip=directMode?"N/A":(WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():"--");
  String displayHost=directMode?"N/A - ESP-NOW":emulatorHost;
  bool hostValid=displayHost.length()>0;
  for(size_t i=0;i<displayHost.length();i++) if((uint8_t)displayHost[i]<32 || (uint8_t)displayHost[i]>126) { hostValid=false; break; }
  if(!hostValid) displayHost="INVALID - RESET NETWORK";
  snprintf(b,sizeof(b),"DISPLAY FW       %s\nCONNECTION       %s\nDISPLAY IP       %s\nEMULATOR IP      %s\nRADIO CHANNEL    %u\nBATTERIES        %u\nESP-NOW RX       %lu\nDATA AGE         %s\nCELL DATA AGE    %s\nUPTIME           %lu min",FW_VERSION,directMode?"ESP-NOW DIRECT":"WI-FI + WEB",ip.c_str(),displayHost.c_str(),directMode?espNowChannel:(uint8_t)WiFi.channel(),espNowBatteryCount,(unsigned long)espNowPackets,ageText(lastGood).c_str(),ageText(cells.lastGood).c_str(),(unsigned long)(millis()/60000)); lv_label_set_text(uSystemInfo,b);
  if(controlActionResult.length()) {
    lv_obj_t *toast=uDtcVisible?uDtcToast:uActionToast;
    lv_label_set_text(toast,controlActionResult.c_str()); lv_obj_clear_flag(toast,LV_OBJ_FLAG_HIDDEN);
  }

  uint8_t packCount=constrain((int)espNowBatteryCount,1,3);
  bool multi=packCount>1;
  if(multi){
    lv_obj_clear_flag(uDualDash,LV_OBJ_FLAG_HIDDEN);
    for(int i=0;i<3;i++){
      if(i>=packCount){ lv_obj_add_flag(uDualDashCard[i],LV_OBJ_FLAG_HIDDEN); continue; }
      lv_obj_clear_flag(uDualDashCard[i],LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_y(uDualDashCard[i],packCount==2?72+i*184:68+i*124);
      lv_obj_set_height(uDualDashCard[i],packCount==2?172:116);
      Telemetry &bt=batteryData[i];
      if(!batteryLastGood[i] || millis()-batteryLastGood[i]>=OFFLINE_AGE_MS){ lv_label_set_text(uDualDashText[i],"WAITING FOR DATA"); continue; }
      float pwr=(isnan(bt.voltage)||isnan(bt.current))?NAN:bt.voltage*bt.current/1000.0f;
      snprintf(b,sizeof(b),"SOC %.1f%%   REAL %.1f%%   SOH %.0f%%\n%.1f V   %+.2f kW   %+.1f A\nCELLS %.3f-%.3f V   CELL DELTA %u mV   TEMP %.1f-%.1f C",bt.soc,bt.realSoc,bt.soh,bt.voltage,pwr,bt.current,bt.cMin/1000.0f,bt.cMax/1000.0f,bt.delta,bt.tMin,bt.tMax);
      lv_label_set_text(uDualDashText[i],b);
      lv_obj_set_style_text_color(uDualDashText[i],bt.current>0.05f?U_GREEN():(bt.current<-0.05f?U_BLUE():lv_color_white()),0);
    }
  } else lv_obj_add_flag(uDualDash,LV_OBJ_FLAG_HIDDEN);

  bool multiCells=multi;
  if(multiCells){
    lv_obj_clear_flag(uDualCells,LV_OBJ_FLAG_HIDDEN);
    for(int i=0;i<3;i++){
      if(i>=packCount){ lv_obj_add_flag(uDualCellCard[i],LV_OBJ_FLAG_HIDDEN); continue; }
      lv_obj_clear_flag(uDualCellCard[i],LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_y(uDualCellCard[i],packCount==2?72+i*184:68+i*124);
      lv_obj_set_height(uDualCellCard[i],packCount==2?172:116);
      lv_obj_set_y(uDualCellPlot[i],packCount==2?48:38);
      lv_obj_set_height(uDualCellPlot[i],packCount==2?100:65);
      CellMonitorData &cs=batteryCells[i]; Telemetry &bt=batteryData[i];
      if(!cs.valid){ lv_label_set_text(uDualCellText[i],"WAITING FOR CELL DATA"); continue; }
      snprintf(b,sizeof(b),"MIN %u(#%u)  MAX %u(#%u)  CELL DELTA %u mV  TEMP %.1f-%.1f C",cs.minMv,cs.minCell+1,cs.maxMv,cs.maxCell+1,cs.deltaMv,bt.tMin,bt.tMax);
      lv_label_set_text(uDualCellText[i],b); lv_obj_invalidate(uDualCellPlot[i]);
    }
  } else lv_obj_add_flag(uDualCells,LV_OBJ_FLAG_HIDDEN);
  if(uDtcVisible && uDtcPage) lv_obj_move_foreground(uDtcPage);
  // Calibration must stay above the connection wizard until all three raw
  // GT911 taps have been captured. Previously ui480Update() repeatedly moved
  // the wizard over the calibration overlay, making the targets invisible.
  if(touchCalibrationActive && touchCalOverlay) lv_obj_move_foreground(touchCalOverlay);
  else if(onboardingNeeded && uWizard) lv_obj_move_foreground(uWizard);
}

#endif
