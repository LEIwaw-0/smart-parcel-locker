#ifndef __APP_UI_H
#define __APP_UI_H

/******************************************************
**  app_ui.h - title bar, clock/weather cards, menu, overlays,
**             DONE screens and the shared status bar.
**
**  Entry points used by other modules:
**    StatusBar_Set()        - status text + repaint (all modules)
**    Menu_Draw()            - repaint the main menu
**    Menu_DrawVoxHint()     - voice-link hint on the menu
**    Menu_DrawChatPanel()   - AI conversation overlay
**    Menu_DrawPickupGuide() - voice pickup guide overlay
**    Menu_DrawAdmin()       - admin code list
**    DepositDone_Draw() / PickupDone_Draw()
**    PickBtn_Draw() / PickBtn_Scan()
**    Guide_ShowPartial()    - shared partial-code hint helper
******************************************************/
#include "app_config.h"

void    StatusBar_Set(uint8_t src, uint8_t box_id, const char *msg);
void    TitleBar_DrawLink(void);
void    Menu_DrawVoxHint(const char *raw);
void    Menu_DrawChatPanel(void);
void    Guide_ShowPartial(void);
void    Menu_DrawPickupGuide(void);
void    Menu_DrawAdmin(void);
void    Menu_Draw(void);
void    DepositDone_Draw(void);
void    PickupDone_Draw(void);
void    PickBtn_Draw(uint8_t pressed);
uint8_t PickBtn_Scan(uint8_t touch_down, uint16_t x, uint16_t y);

#endif /* __APP_UI_H */
