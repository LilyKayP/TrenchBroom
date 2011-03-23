//
//  SelectionLayer.h
//  TrenchBroom
//
//  Created by Kristian Duske on 08.02.11.
//  Copyright 2011 __MyCompanyName__. All rights reserved.
//

#import "GeometryLayer.h"

@class GridRenderer;
@class Options;

@interface SelectionLayer : GeometryLayer {
    GridRenderer* gridRenderer;
    Options* options;
}

@end
