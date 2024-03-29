import os.path
import tempfile


class AutoDoc ( object ):
    
    newlines = '\n'
    
    def __init__( self, prjinfo, srcpath, slient=False ):
        
        if 'project' not in prjinfo :
            raise Exception, 'ProjectName must provied'
        
        self.prjpath = prjinfo.get( 'path', prjinfo['project'] )
        self.prjpath = os.path.join(srcpath,self.prjpath)
        #print 'prjpath', self.prjpath
        
        self.srcpath = prjinfo.get('autodoc', None)
        if self.srcpath is not None :
            self.srcpath = os.path.join(self.prjpath,self.srcpath)
        self.codec = prjinfo.get('codec', 'utf-8').lower()
        self.fileexts = ['.c','.h']
        self.doctype = prjinfo.get('txt', 'rst').lower()
        self.template = prjinfo.get('template','template')
        
        self.prjinfo = prjinfo
        self.prjinfo.setdefault('auth','unkown')
        
        if slient :
            self.printinfo = self.slient
            self.printdoc = self.slient
        
    @staticmethod
    def findjust( doc ):
        
        doc = [ (len(d) - len(d.lstrip())) for d in doc if len(d) != 0 ]
        return min( doc )
        
    @staticmethod
    def have_autodoc_sign( ln ):
        
        return ( ln.startswith('/*') \
                 and ln.lstrip('/* ').lower().startswith('autodoc')
               )
        
    @staticmethod
    def slient( *args, **kwargs ):
        return
        
    @staticmethod
    def printinfo( concept, name, filename, lineno ):
        print '  line', lineno, ':', concept, name
        
    @staticmethod
    def printdoc( filename ):
        print 'file', filename, ':'
        
    def readlines( self, fp ):
        c = fp.read()
        if self.codec != 'utf-8' :
            c = c.decode(self.codec).encode('utf-8')
        return c.splitlines()
        
    def parse_doc( self, lns, i ):
        
        j = i
        while( j < len(lns) and '*/' not in lns[j] ):
            j += 1
        
        if i != j :
            doc = [ lns[i].split('/*',1)[-1].split('autodoc')[-1] ]\
                  + lns[i+1:j]\
                  + [lns[j].split('*/',1)[0] ]
        else :
            doc = lns[i].split('/*',1)[-1].split('autodoc')[-1]\
                                          .split('*/',1)[0]
        
        doc = [ d.expandtabs(4).rstrip() for d in doc ]
        ljust_len = self.findjust( doc )
        
        p = '| ' if self.doctype == 'txt' else ''
        
        if ljust_len != 0 :
            doc = [ ' '*4 + p + d[ljust_len:] for d in doc ]
        else :
            doc = [ ' '*4 + p + d for d in doc ]
        
        while( len(doc) > 0 and doc[0] == '' ):
            doc.pop(0)
        while( len(doc) > 0 and doc[-1] == '' ):
            doc.pop(-1)
        
        doc = self.newlines.join( doc )
        
        return j, doc 
        
    @staticmethod
    def readtosign( lns, i, l, c ):
        
        j = i
        dls = []
        while( c not in l ):
            dls.append(l)
            j += 1
            l = lns[j]
        dls.append(l.split(c)[0])
        
        return j, dls
        
    def parse_define( self, lns, i ):
        
        l = lns[i].split('*/',1)[-1].strip()
        
        while( l == '' ):
            i += 1
            l = lns[i].strip()
            
        l = l.strip()
            
        if l.startswith('#'):
            return i, i, 'macro', l.split()[1], None
            
        if l.startswith('typedef') :
            
            if l[7:].strip().startswith('struct') :
                
                j, dls = self.readtosign( lns, i, l, '}' )
                
                l2 = lns[j].split('}',1)[-1]
                
                j, dls = self.readtosign( lns, j, l2, ';' )
            
            else :
                
                j, dls = self.readtosign( lns, i, l, ';' )
            
            n = ''.join(dls).rsplit(None,1)[-1]
            
            return i, j, 'type', n, [l]+lns[i+1:j+1]
            
        if l.startswith('struct'):
            
            j, dls = self.readtosign( lns, i, l, '}' )
            
            n = ''.join( x.strip() for x in dls )
            n = n.split(None,2)[1]
            
            return i, j, 'type', n, [l]+lns[i+1:j+1]
        
        j, dls = self.readtosign( lns, i, l, '{' )
        
        fdef = ' '.join( x.strip() for x in dls )
        
        return i, j, 'function', fdef, None
        
    def parser( self, arg, path, fname ):
        
        self.printdoc( fname )
        
        with open( path, 'r' ) as fp :
            
            lns = self.readlines(fp)
            
            i = 0
            while( i < len(lns) ):
                
                if self.have_autodoc_sign(lns[i]) :
                    
                    i, doc = self.parse_doc( lns, i )
                    
                    lineno, i, dt, n, c_def = self.parse_define( lns, i )
                    
                    self.printinfo( dt, n, fname, lineno )
                    
                    rstdef = '.. c:' + dt + ':: ' + n
                    
                    if c_def :
                        c_def = ['    ','    ::',' '*8] \
                                + [' '*8+l for l in c_def ] + ['    ']
                    else :
                        c_def = []
                    
                    if dt not in arg :
                        arg[dt] = []
                    
                    arg[dt].append( 
                        self.newlines.join( [rstdef] + c_def + \
                                            [doc, self.newlines] )
                    )
                    
                    i = i + 1
                    
                else :
                    
                    i += 1
        
        return
        
    def step( self, arg, dirname, names ):
        
        for n in names :
            
            for ext in self.fileexts :
                if n.endswith(ext):
                    break
            else :
                continue
            
            fn = os.path.join( dirname, n )
            
            if os.path.isfile(fn) :
                
                self.parser( arg, fn, n )
            
        return
        
    def compile( self, outfile ):
        
        autodocs = {}
        
        #print self.srcpath
        if self.srcpath :
            os.path.walk( self.srcpath, self.step, autodocs )
        
        for n in ('README','README.txt','README.rst') :
            intro = os.path.join(self.prjpath,n)
            if os.path.exists(intro) and os.path.isfile(intro) :
                break
        else :
            intro = None
            
        if intro :
            print intro
            with open( intro, 'rU' ) as fp :
                intro = self.readlines(fp)
                if self.doctype == 'txt' :
                    intro = [ '| ' + i for i in intro ]
                else :
                    intro = self.newlines.join(intro)
        else :
            intro = ''
            
        
        with open( self.template + '.xrst', 'rU' ) as fp :
            formatter = fp.read()
        
        info = self.prjinfo.copy()
        info['Intro'] = intro
        info['Fuctions'] = self.newlines.join( autodocs.get('function',[]) )
        info['DataType'] = self.newlines.join( autodocs.get('type',[]) )
        
        with open( outfile, 'w' ) as fp :
            fp.write( formatter % info )
        
        return
        