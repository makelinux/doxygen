/******************************************************************************
 *
 * 
 *
 * Copyright (C) 1997-2000 by Dimitri van Heesch.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation under the terms of the GNU General Public License is hereby 
 * granted. No representations are made about the suitability of this software 
 * for any purpose. It is provided "as is" without express or implied warranty.
 * See the GNU General Public License for more details.
 *
 * Documents produced by Doxygen are derivative works derived from the
 * input used in their production; they are not affected by this license.
 *
 */

#include <stdlib.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "qtbc.h"
#include <qfile.h>
#include <qtextstream.h>
#include <qfileinfo.h>
#include <qdir.h>

#include "formula.h"
#include "image.h"
#include "util.h"
#include "message.h"
#include "config.h"

Formula::Formula(const char *text)
{
  static int count=0;
  number = count++;
  form=text;
}

Formula::~Formula()
{
}

int Formula::getId()
{
  return number;
}

void FormulaList::generateBitmaps(const char *path)
{
  int x1,y1,x2,y2;
  QDir d(path);
  // store the original directory
  if (!d.exists()) { err("Error: Output dir %s does not exist!\n",path); exit(1); }
  QCString oldDir = convertToQCString(QDir::currentDirPath());
  // goto the html output directory (i.e. path)
  QDir::setCurrent(d.absPath());
  QDir thisDir;
  // generate a latex file containing one formula per page.
  QCString texName="_formulas.tex";
  QList<int> pagesToGenerate;
  pagesToGenerate.setAutoDelete(TRUE);
  FormulaListIterator fli(*this);
  Formula *formula;
  QFile f(texName);
  bool formulaError=FALSE;
  if (f.open(IO_WriteOnly))
  {
    QTextStream t(&f);
    if (Config::latexBatchModeFlag) t << "\\batchmode" << endl;
    t << "\\documentclass{article}" << endl;
    t << "\\usepackage{epsfig}" << endl; // for those who want to include images
    const char *s=Config::extraPackageList.first();
    while (s)
    {
      t << "\\usepackage{" << s << "}\n";
      s=Config::extraPackageList.next();
    }
    t << "\\pagestyle{empty}" << endl; 
    t << "\\begin{document}" << endl;
    int page=0;
    for (fli.toFirst();(formula=fli.current());++fli)
    {
      QCString resultName;
      resultName.sprintf("form-%d.gif",formula->getId());
      // only formulas for which no image exists are generated
      QFileInfo fi(resultName);
      if (!fi.exists())
      {
        // we force a pagebreak after each formula
        t << formula->getFormulaText() << endl << "\\pagebreak\n\n";
        pagesToGenerate.append(new int(page));
      }
      page++;
    }
    t << "\\end{document}" << endl;
    f.close();
  }
  if (pagesToGenerate.count()>0) // there are new formulas
  {
    //printf("Running latex...\n");
    //system("latex _formulas.tex </dev/null >/dev/null");
    if (iSystem("latex _formulas.tex")!=0)
    {
      err("Problems running latex. Check your installation or look for typos in _formulas.tex!\n");
      formulaError=TRUE;
      //return;
    }
    //printf("Running dvips...\n");
    QListIterator<int> pli(pagesToGenerate);
    int *pagePtr;
    int pageIndex=1;
    for (;(pagePtr=pli.current());++pli,++pageIndex)
    {
      int pageNum=*pagePtr;
      msg("Generating image form-%d.gif for formula\n",pageNum);
      char dviCmd[256];
      QCString formBase;
      formBase.sprintf("_form%d",pageNum);
      // run dvips to convert the page with number pageIndex to an
      // encapsulated postscript.
      sprintf(dviCmd,"dvips -q -D 600 -E -n 1 -p %d -o %s.eps _formulas.dvi",
          pageIndex,formBase.data());
      if (iSystem(dviCmd)!=0)
      {
        err("Problems running dvips. Check your installation!\n");
        return;
      }
      // now we read the generated postscript file to extract the bounding box
      QFileInfo fi(formBase+".eps");
      if (fi.exists())
      {
        QCString eps = fileToString(formBase+".eps");
        int i=eps.find("%%BoundingBox:");
        if (i!=-1)
        {
          sscanf(eps.data()+i,"%%%%BoundingBox:%d %d %d %d",&x1,&y1,&x2,&y2);
        }
        else
        {
          err("Error: Couldn't extract bounding box!\n");
        }
      } 
      // next we generate a postscript file which contains the eps
      // and displays it in the right colors and the right bounding box
      f.setName(formBase+".ps");
      if (f.open(IO_WriteOnly))
      {
        QTextStream t(&f);
        t << "1 1 1 setrgbcolor" << endl;  // anti-alias to white background
        t << "newpath" << endl;
        t << "-1 -1 moveto" << endl;
        t << (x2-x1+2) << " -1 lineto" << endl;
        t << (x2-x1+2) << " " << (y2-y1+2) << " lineto" << endl;
        t << "-1 " << (y2-y1+2) << " lineto" <<endl;
        t << "closepath" << endl;
        t << "fill" << endl;
        t << -x1 << " " << -y1 << " translate" << endl;
        t << "0 0 0 setrgbcolor" << endl;
        t << "(" << formBase << ".eps) run" << endl;
        f.close();
      }
      // scale the image so that it is four times larger than needed.
      // and the sizes are a multiple of four.
      const double scaleFactor = 16.0/3.0; 
      int gx = (((int)((x2-x1)*scaleFactor))+3)&~2;
      int gy = (((int)((y2-y1)*scaleFactor))+3)&~2;
      // Then we run ghostscript to convert the postscript to a pixmap
      // The pixmap is a truecolor image, where only black and white are
      // used.  
#ifdef _WIN32
      char gsArgs[256];
      sprintf(gsArgs,"-q -g%dx%d -r%dx%dx -sDEVICE=ppmraw "
                     "-sOutputFile=%s.pnm -DNOPAUSE -- %s.ps",
                     gx,gy,(int)(scaleFactor*72),(int)(scaleFactor*72),
                     formBase.data(),formBase.data()
             );
      // gswin32 is a GUI api which will pop up a window and run
      // asynchronously. To prevent both, we use ShellExecuteEx and
      // WaitForSingleObject (thanks to Robert Golias for the code)
      SHELLEXECUTEINFO sInfo = {
        sizeof(SHELLEXECUTEINFO),   /* structure size */
        SEE_MASK_NOCLOSEPROCESS,    /* leave the process running */
        NULL,                       /* window handle */
        NULL,                       /* action to perform: open */
        "gswin32.exe",              /* file to execute */
        gsArgs,                     /* argument list */ 
        NULL,                       /* use current working dir */
        SW_HIDE,                    /* minimize on start-up */
        0,                          /* application instance handle */
        NULL,                       /* ignored: id list */
        NULL,                       /* ignored: class name */
        NULL,                       /* ignored: key class */
        0,                          /* ignored: hot key */
        NULL,                       /* ignored: icon */
        NULL                        /* resulting application handle */
      };
      if (!ShellExecuteEx(&sInfo))
      {
        err("Problem running ghostscript. Check your installation!\n");
        return;
      }
      else if (sInfo.hProcess)      /* executable was launched, wait for it to finish */
      {
        WaitForSingleObject(sInfo.hProcess,INFINITE); 
      }
#else
      char gsCmd[256];
      sprintf(gsCmd,"gs -q -g%dx%d -r%dx%dx -sDEVICE=ppmraw "
                    "-sOutputFile=%s.pnm -DNOPAUSE -- %s.ps",
                    gx,gy,(int)(scaleFactor*72),(int)(scaleFactor*72),
                    formBase.data(),formBase.data()
             );
      if (iSystem(gsCmd)!=0)
      {
        err("Problem running ghostscript. Check your installation!\n");
        return;
      }
#endif
      f.setName(formBase+".pnm");
      uint imageX=0,imageY=0;
      // we read the generated image again, to obtain the pixel data.
      if (f.open(IO_ReadOnly))
      {
        QTextStream t(&f);
        QCString s;
        if (!t.eof())
          s=t.readLine();
        if (s.length()<2 || s.left(2)!="P6")
          err("Error: ghostscript produced an illegal image format!");
        else
        {
          // assume the size if after the first line that does not start with
          // # excluding the first line of the file.
          while (!t.eof() && (s=t.readLine()) && !s.isEmpty() && s.at(0)=='#');
          sscanf(s,"%d %d",&imageX,&imageY);
        }
        if (imageX>0 && imageY>0)
        {
          //printf("Converting image...\n");
          char *data = new char[imageX*imageY*3]; // rgb 8:8:8 format
          uint i,x,y,ix,iy;
          f.readBlock(data,imageX*imageY*3);
          Image srcImage(imageX,imageY),
                filteredImage(imageX,imageY),
                dstImage(imageX/4,imageY/4);
          uchar *ps=srcImage.getData();
          // convert image to black (1) and white (0) index.
          for (i=0;i<imageX*imageY;i++) *ps++= (data[i*3]==0 ? 1 : 0);
          // apply a simple box filter to the image 
          static int filterMask[]={1,2,1,2,8,2,1,2,1};
          for (y=0;y<srcImage.getHeight();y++)
          {
            for (x=0;x<srcImage.getWidth();x++)
            {
              int s=0;
              for (iy=0;iy<2;iy++)
              {
                for (ix=0;ix<2;ix++)
                {
                  s+=srcImage.getPixel(x+ix-1,y+iy-1)*filterMask[iy*3+ix];
                }
              }
              filteredImage.setPixel(x,y,s);
            }
          }
          // down-sample the image to 1/16th of the area using 16 gray scale
          // colors.
          for (y=0;y<dstImage.getHeight();y++)
          {
            for (x=0;x<dstImage.getWidth();x++)
            {
              int xp=x<<2;
              int yp=y<<2;
              int c=filteredImage.getPixel(xp+0,yp+0)+
                    filteredImage.getPixel(xp+1,yp+0)+
                    filteredImage.getPixel(xp+2,yp+0)+
                    filteredImage.getPixel(xp+3,yp+0)+
                    filteredImage.getPixel(xp+0,yp+1)+
                    filteredImage.getPixel(xp+1,yp+1)+
                    filteredImage.getPixel(xp+2,yp+1)+
                    filteredImage.getPixel(xp+3,yp+1)+
                    filteredImage.getPixel(xp+0,yp+2)+
                    filteredImage.getPixel(xp+1,yp+2)+
                    filteredImage.getPixel(xp+2,yp+2)+
                    filteredImage.getPixel(xp+3,yp+2)+
                    filteredImage.getPixel(xp+0,yp+3)+
                    filteredImage.getPixel(xp+1,yp+3)+
                    filteredImage.getPixel(xp+2,yp+3)+
                    filteredImage.getPixel(xp+3,yp+3);
              // here we scale and clip the color value so the
              // resulting image has a reasonable contrast
              dstImage.setPixel(x,y,QMIN(15,(c*15)/(16*10)));
            }
          }
          // save the result as a gif
          QCString resultName;
          resultName.sprintf("form-%d.gif",pageNum);
          // the option parameter 1 is used here as a temporary hack
          // to select the right color palette! 
          dstImage.save(resultName,1);
          delete[] data;
        }
        f.close();
      } 
      // remove intermediate image files
      thisDir.remove(formBase+".eps");
      thisDir.remove(formBase+".pnm");
      thisDir.remove(formBase+".ps");
    }
    // remove intermediate files produced by latex
    thisDir.remove("_formulas.dvi");
    thisDir.remove("_formulas.log");
    thisDir.remove("_formulas.aux");
  }
  // remove the latex file itself
  if (!formulaError) thisDir.remove("_formulas.tex");
  // write/update the formula repository so we know what text the 
  // generated gifs represent (we use this next time to avoid regeneration
  // of the gifs, and to avoid forcing the user to delete all gifs in order
  // to let a browser refresh the images).
  f.setName("formula.repository");
  if (f.open(IO_WriteOnly))
  {
    QTextStream t(&f);
    for (fli.toFirst();(formula=fli.current());++fli)
    {
      t << "\\form#" << formula->getId() << ":" << formula->getFormulaText() << endl;
    }
    f.close();
  }
  // reset the directory to the original location.
  QDir::setCurrent(oldDir);
}


#ifdef FORMULA_TEST
int main()
{
  FormulaList fl;
  fl.append(new Formula("$x^2$"));
  fl.append(new Formula("$y^2$"));
  fl.append(new Formula("$\\sqrt{x_0^2+x_1^2+x_2^2}$"));
  fl.generateBitmaps("dest");
  return 0;
}
#endif
