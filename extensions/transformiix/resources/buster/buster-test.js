/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is TransforMiiX XSLT Processor.
 *
 * The Initial Developer of the Original Code is
 * Axel Hecht.
 * Portions created by the Initial Developer are Copyright (C) 2002
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Axel Hecht <axel@pike.org>
 *  Peter Van der Beken <peterv@netscape.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

var parser = new DOMParser();

var runQueue = 
{
    mArray : new Array(),
    push : function(aRunItem)
    {
        this.mArray.push(aRunItem);
    },
    observe : function(aSubject, aTopic, aData)
    {
        var item = this.mArray.shift();
        if (item) {
            item.run(this);
        }
    },
    run : function()
    {
        this.observe(null,'','');
    }
}

var itemCache = 
{
    mArray : new Array(),
    getItem : function(aResource)
    {
        // Directory selected
        if (kContUtils.IsSeq(runItem.prototype.kDatabase, aResource)) {
            var aSeq = kContUtils.MakeSeq(runItem.prototype.kDatabase, aResource);
            dump("sequence: "+aSeq+" with "+aSeq.GetCount()+" elements\n");
            var child, children = aSeq.GetElements();
            var m = 0, first;
            while (children.hasMoreElements()) {
                m += 1;
                child = children.getNext();
                child.QueryInterface(nsIRDFResource);
                if (!first)
                    first = itemCache.getItem(child);
                else
                    itemCache.getItem(child);
            }
            return first;
        }
        if (aResource.Value in this.mArray) {
            return this.mArray[aResource.Value];
        }
        var retItem = new runItem(aResource);
        this.mArray[aResource.Value] = retItem;
        runQueue.push(retItem);
        return retItem;
    },
    rerunItem : function(aResource, aObserver)
    {
        var anItem = new runItem(aResource);
        this.mArray[aResource.Value] = anItem;
        anItem.run(aObserver);
    },
    observe : function(aSubject, aTopic, aData)
    {
        this.mRun += 1;
        if (aTopic == "success") {
            if (aData == "yes") {
                this.mGood += 1;
            }
            else {
                this.mFalse +=1;
            }
        }
    }
}

function runItem(aResource)
{
  this.mResource = aResource;
  // Directory selected
  if (kContUtils.IsSeq(this.kDatabase,this.mResource)) {
      var aSeq = kContUtils.MakeSeq(this.kDatabase,this.mResource);
      dump("THIS SHOULDN'T HAPPEN\n");
      var child, children = aSeq.GetElements();
      var m = 0;
      while (children.hasMoreElements()) {
          m += 1;
          child = children.getNext();
          child.QueryInterface(nsIRDFResource);
          itemCache.getItem(child);
      }
  }
}

runItem.prototype = 
{
    // RDF resource associated with this test
    mResource : null,
    // XML documents for the XSLT transformation
    mSourceDoc : null,
    mStyleDoc  : null,
    mResDoc    : null,
    // XML or plaintext document for the reference
    mRefDoc    : null,
    // bitfield signaling the loaded documents
    mLoaded    : 0,
    kSource    : 1,
    kStyle     : 2,
    kReference : 4,
    // a observer, potential argument to run()
    mObserver  : null,
    mSuccess   : null,
    mMethod    : 'xml',
    // XSLTProcessor, shared by the instances
    kProcessor : new XSLTProcessor(),
    kXalan     : kStandardURL.createInstance(nsIURL),
    kDatabase  : null,
    kObservers : new Array(),

    run : function(aObserver)
    {
        if (aObserver && typeof(aObserver)=='function' ||
            (typeof(aObserver)=='object' && 
             typeof(aObserver.observe)=='function')) {
            this.mObserver=aObserver;
        }
        var name = this.kDatabase.GetTarget(this.mResource, krTypeName, true);
        if (name) {
            var cat = this.kDatabase.GetTarget(this.mResource, krTypeCat, true);
            var path = this.kDatabase.GetTarget(this.mResource, krTypePath, true);
            cat = cat.QueryInterface(nsIRDFLiteral);
            name = name.QueryInterface(nsIRDFLiteral);
            path = path.QueryInterface(nsIRDFLiteral);
            var xalan_fl  = this.kXalan.resolve(cat.Value+"/"+path.Value);
            var xalan_ref  = this.kXalan.resolve(cat.Value+"-gold/"+path.Value);
            dump(name.Value+" links to "+xalan_fl+"\n");
        }
        // Directory selected
        if (kContUtils.IsSeq(this.kDatabase,this.mResource)) {
            return;
            var aSeq = kContUtils.MakeSeq(this.kDatabase,this.mResource);
            dump("sequence: "+aSeq+" with "+aSeq.GetCount()+" elements\n");
            var child, children = aSeq.GetElements();
            var m = 0;
            while (children.hasMoreElements()) {
                m += 1;
                child = children.getNext();
                child.QueryInterface(nsIRDFResource);
            }
        }
        var refContent = this.loadTextFile(xalan_ref+".out");
        var iframe;
        if (refContent.match(/^<\?xml/)) {
            this.mRefDoc = parser.parseFromString(refContent, 'text/xml');
        }
        else if (refContent.match(/^\s*<html/gi)) {
            this.mMethod = 'html';
            iframe = document.getElementById('hiddenHtml').contentDocument;
            iframe.documentElement.innerHTML = refContent;
            this.mRefDoc = iframe;
        }
        else {
            iframe = document.getElementById('hiddenHtml').contentDocument;
            iframe.documentElement.innerHTML = refContent;
            if (iframe.documentElement.firstChild) {
                this.mMethod = 'html';
                this.mRefDoc = iframe;
            }
        }
        this.mSourceDoc = document.implementation.createDocument('', '', null);
        this.mSourceDoc.addEventListener("load",this.onload(1),false);
        this.mSourceDoc.load(xalan_fl+".xml");
        this.mStyleDoc = document.implementation.createDocument('', '', null);
        this.mStyleDoc.addEventListener("load",this.onload(2),false);
        this.mStyleDoc.load(xalan_fl+".xsl");
    },

    // onload handler helper
    onload : function(file)
    {
        var self = this;
        return function(e)
        {
            return self.fileLoaded(file);
        };
    },

    fileLoaded : function(mask)
    {
        this.mLoaded += mask;
        if (this.mLoaded < 3) {
            return;
        }
        this.mResDoc = document.implementation.createDocument("", "", null);
        this.kProcessor.transformDocument(this.mSourceDoc,
                                          this.mStyleDoc,
                                          this.mResDoc, null);
        this.mRefDoc.normalize();
        try {
            isGood = DiffDOM(this.mResDoc.documentElement,
                             this.mRefDoc.documentElement,
                             this.mMethod == 'html');
        } catch (e) {
            isGood = false;
        };
        dump("This succeeded. "+isGood+"\n");
        isGood = isGood.toString();
        for (var i=0; i<this.kObservers.length; i++) {
            var aObs = this.kObservers[i];
            if (typeof(aObs)=='object' && typeof(aObs.observe)=='function') {
                aObs.observe(this.mResource, 'success', isGood);
            }
            else if (typeof(aObs)=='function') {
                aObs(this.mResource, 'success', isGood);
            }
        }
        if (this.mObserver) {
            if (typeof(this.mObserver)=='object') {
                this.mObserver.observe(this.mResource, 'success', isGood);
            }
            else {
                this.mObserver(this.mResource, 'success', isGood);
            }
        }
    },

    loadTextFile : function(url)
    {
        var serv = Components.classes[IOSERVICE_CTRID].
            getService(nsIIOService);
        if (!serv) {
            throw Components.results.ERR_FAILURE;
        }
        var chan = serv.newChannel(url, null, null);
        var instream = doCreate(SIS_CTRID, nsISIS);
        instream.init(chan.open());

        return instream.read(instream.available());
    }
}

runItem.prototype.kXalan.QueryInterface(nsIStandardURL);

var cmdTestController = 
{
    supportsCommand: function(aCommand)
    {
        switch(aCommand) {
            case 'cmd_tst_run':
            case 'cmd_tst_runall':
                return true;
            default:
        }
        return false;
    },
    isCommandEnabled: function(aCommand)
    {
        return this.supportsCommand(aCommand);
    },
    doCommand: function(aCommand)
    {
        switch(aCommand) {
            case 'cmd_tst_run':
                dump("cmd_tst_run\n");
                break;
            case 'cmd_tst_runall':
                dump("cmd_tst_runall\n");
                var tst_run = document.getElementById('cmd_tst_run');
                tst_run.doCommand();
            default:
        }
    }
};

registerController(cmdTestController);
