/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/** Helper functions for identity mochitests **/
/** Please keep functions in-sync with unit/head_identity.js **/

"use strict";

const Cc = SpecialPowers.wrap(Components).classes;
const Ci = Components.interfaces;
const Cu = SpecialPowers.wrap(Components).utils;

const TEST_URL = "http://mochi.test:8888";
const TEST_URL2 = "https://myfavoritebaconinacan.com";
const TEST_USER = "user@mozilla.com";
const TEST_PRIVKEY = "fake-privkey";
const TEST_CERT = "fake-cert";
const TEST_IDPPARAMS = {
  domain: "myfavoriteflan.com",
  authentication: "/foo/authenticate.html",
  provisioning: "/foo/provision.html"
};

const Services = Cu.import("resource://gre/modules/Services.jsm").Services;

// Set the debug pref before loading other modules
Services.prefs.setBoolPref("toolkit.identity.debug", true);
Services.prefs.setBoolPref("dom.identity.enabled", true);

const jwcrypto = Cu.import("resource://gre/modules/identity/jwcrypto.jsm").jwcrypto;
const IdentityStore = Cu.import("resource://gre/modules/identity/IdentityStore.jsm").IdentityStore;
const RelyingParty = Cu.import("resource://gre/modules/identity/RelyingParty.jsm").RelyingParty;
const XPCOMUtils = Cu.import("resource://gre/modules/XPCOMUtils.jsm").XPCOMUtils;
const IDService = Cu.import("resource://gre/modules/identity/Identity.jsm").IdentityService;
const IdentityProvider = Cu.import("resource://gre/modules/identity/IdentityProvider.jsm").IdentityProvider;

const identity = navigator.mozId;

function do_check_null(aVal, aMsg) {
  is(aVal, null, aMsg);
}

function do_timeout(aDelay, aFunc) {
  setTimeout(aFunc, aDelay);
}

function get_idstore() {
  return IdentityStore;
}

// create a mock "watch" object, which the Identity Service
function mock_watch(aIdentity, aDoFunc) {
  function partial(fn) {
    let args = Array.prototype.slice.call(arguments, 1);
    return function() {
      return fn.apply(this, args.concat(Array.prototype.slice.call(arguments)));
    };
  }

  let mockedWatch = {};
  mockedWatch.loggedInEmail = aIdentity;
  mockedWatch['do'] = aDoFunc;
  mockedWatch.onready = partial(aDoFunc, 'ready');
  mockedWatch.onlogin = partial(aDoFunc, 'login');
  mockedWatch.onlogout = partial(aDoFunc, 'logout');

  return mockedWatch;
}

// mimicking callback funtionality for ease of testing
// this observer auto-removes itself after the observe function
// is called, so this is meant to observe only ONE event.
function makeObserver(aObserveTopic, aObserveFunc) {
  let observer = {
    // nsISupports provides type management in C++
    // nsIObserver is to be an observer
    QueryInterface: XPCOMUtils.generateQI([Ci.nsISupports, Ci.nsIObserver]),

    observe: function (aSubject, aTopic, aData) {
      if (aTopic == aObserveTopic) {
        try {
          aObserveFunc(SpecialPowers.wrap(aSubject), aTopic, aData);
        } catch (ex) {
          ok(false, ex);
        }
        Services.obs.removeObserver(observer, aObserveTopic);
      }
    }
  };

  Services.obs.addObserver(observer, aObserveTopic, false);
}

// set up the ID service with an identity with keypair and all
// when ready, invoke callback with the identity
function setup_test_identity(identity, cert, cb) {
  // set up the store so that we're supposed to be logged in
  let store = get_idstore();

  function keyGenerated(err, kpo) {
    store.addIdentity(identity, kpo, cert);
    cb();
  };

  jwcrypto.generateKeyPair("DS160", keyGenerated);
}

// takes a list of functions and returns a function that
// when called the first time, calls the first func,
// then the next time the second, etc.
function call_sequentially() {
  let numCalls = 0;
  let funcs = arguments;

  return function() {
    if (!funcs[numCalls]) {
      let argString = Array.prototype.slice.call(arguments).join(",");
      ok(false, "Too many calls: " + argString);
      return;
    }
    funcs[numCalls].apply(funcs[numCalls], arguments);
    numCalls += 1;
  };
}

/*
 * Setup a provisioning workflow with appropriate callbacks
 *
 * identity is the email we're provisioning.
 *
 * afterSetupCallback is required.
 *
 * doneProvisioningCallback is optional, if the caller
 * wants to be notified when the whole provisioning workflow is done
 *
 * frameCallbacks is optional, contains the callbacks that the sandbox
 * frame would provide in response to DOM calls.
 */
function setup_provisioning(identity, afterSetupCallback, doneProvisioningCallback, callerCallbacks) {
  IDService.init();

  let util = window.QueryInterface(Ci.nsIInterfaceRequestor)
                    .getInterface(Ci.nsIDOMWindowUtils);

  let provId = util.outerWindowID;
  IDService.IDP._provisionFlows[provId] = {
    identity : identity,
    idpParams: TEST_IDPPARAMS,
    callback: function(err) {
      if (doneProvisioningCallback)
        doneProvisioningCallback(err);
    },
    sandbox: {
	// Emulate the free() method on the iframe sandbox
	free: function() {}
    }
  };

  let caller = {};
  caller.id = provId;
  caller.doBeginProvisioningCallback = function(id, duration_s) {
    if (callerCallbacks && callerCallbacks.beginProvisioningCallback)
      callerCallbacks.beginProvisioningCallback(id, duration_s);
  };
  caller.doGenKeyPairCallback = function(pk) {
    if (callerCallbacks && callerCallbacks.genKeyPairCallback)
      callerCallbacks.genKeyPairCallback(pk);
  };

  afterSetupCallback(callerCallbacks);
}

function resetState() {
  get_idstore().init();
  RelyingParty.init();
}

var TESTS = [];

function run_next_test() {
  if (TESTS.length) {
    let test = TESTS.shift();
    info(test.name);
    test();
  } else {
    Services.prefs.clearUserPref("toolkit.identity.debug");
    Services.prefs.clearUserPref("dom.identity.enabled");
    SimpleTest.finish();
  }
}
