/*
 Copyright (C) 2010-2012 Kristian Duske
 
 This file is part of TrenchBroom.
 
 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with TrenchBroom.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __TrenchBroom__AbstractApp__
#define __TrenchBroom__AbstractApp__

#include <wx/wx.h>

class wxCommandEvent;
class wxDocManager;
class wxEvtHandler;
class wxMenu;
class wxMenuBar;
class wxView;
class DocManager;

class AbstractApp : public wxApp {
protected:
	DocManager* m_docManager;

    virtual wxMenu* CreateFileMenu();
    virtual wxMenu* CreateEditMenu();
    virtual wxMenu* CreateViewMenu();
    virtual wxMenu* CreateHelpMenu();
public:
    virtual wxMenuBar* CreateMenuBar(wxEvtHandler* eventHandler);

    void UpdateAllViews(wxView* sender = NULL, wxObject* hint = NULL);

	virtual bool OnInit();
    virtual int OnExit();
    void OnUnhandledException();


    DECLARE_EVENT_TABLE();
};

#endif /* defined(__TrenchBroom__AbstractApp__) */
